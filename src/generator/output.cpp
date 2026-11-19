#include "openosc/generator/output.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/interrupts.hpp"

namespace openosc::generator {
namespace {
alignas(32) uint16_t tables[2][1024] __attribute__((section(".generator")));
// TIM13 PWM on PA6 (AF9): the user's square output and the calibration reference, one setup.
void startPwm(const OutputPlan &plan, unsigned dutyPermille) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM13_CLK_ENABLE();
    TIM13->CR1 = 0;
    TIM13->PSC = plan.timer.prescaler;
    TIM13->ARR = plan.timer.reload;
    TIM13->CCR1 = plan.compare;
    TIM13->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE;
    if (dutyPermille == 0)
        TIM13->CCMR1 = 4U << 4;
    if (dutyPermille == 1000)
        TIM13->CCMR1 = 5U << 4;
    TIM13->CCER = TIM_CCER_CC1E;
    TIM13->EGR = TIM_EGR_UG;
    TIM13->SR = 0;
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_6;
    gpio.Pull = GPIO_NOPULL;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF9_TIM13;
    HAL_GPIO_Init(GPIOA, &gpio);
    TIM13->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
}
void stopPwm() {
    TIM13->CR1 &= ~TIM_CR1_CEN;
    TIM13->CCER = 0;
}
void floatJack() {
    GPIO_InitTypeDef pin{};
    pin.Pin = GPIO_PIN_6;
    pin.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &pin);
}
}
bool Output::configure(const Settings &settings) {
    if (configured_ && settings.valid() && settings_.same(settings)) return true;
    OutputPlan plan;
    if (!OutputPlan::calculate(settings, TimerClock::apb1(), plan))
        return false;
    const unsigned next = activeBank_ ^ 1;
    if (!plan.pwm && !WaveTable::build(settings, tables[next], plan.count))
        return false;
    const bool wasEnabled = enabled_;
    if (wasEnabled && !halt())
        return false;
    const auto previousSettings = settings_;
    const auto previousPlan = plan_;
    const auto previousBank = activeBank_;
    const bool wasConfigured = configured_;
    settings_ = settings;
    plan_ = plan;
    activeBank_ = next;
    configured_ = true;
    if (wasEnabled) {
        if (apply()) return true;
        const auto failure = status_;
        if (!halt()) return false;
        settings_ = previousSettings; plan_ = previousPlan; activeBank_ = previousBank; configured_ = wasConfigured;
        if (wasConfigured && apply()) status_ = failure;
        else status_ = DataStatus::HardwareError;
        releaseStopped();
        if (!enabled_) referenceHeld_ = true;
        return false;
    }
    status_ = DataStatus::Ok;
    return true;
}
bool Output::halt() {
    TIM6->CR1 &= ~TIM_CR1_CEN;
    stopPwm();
    DAC2->CR = 0;
    BDMA2_Channel0->CCR &= ~BDMA_CCR_EN;
    for (unsigned wait = 0; (BDMA2_Channel0->CCR & BDMA_CCR_EN) && wait < 10000; ++wait)
        __NOP();
    if (BDMA2_Channel0->CCR & BDMA_CCR_EN) {
        status_ = DataStatus::HardwareError;
        referenceHeld_ = true;
        return false;
    }
    BDMA2->IFCR = BDMA_FLAG_GL0;
    __DSB();
    enabled_ = false;
    floatJack();
    return true;
}
bool Output::apply() {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_TIM6_CLK_ENABLE();
    __HAL_RCC_TIM13_CLK_ENABLE();
    __HAL_RCC_DAC2_CLK_ENABLE();
    __HAL_RCC_BDMA2_CLK_ENABLE();
    if (!halt())
        return false;
    if (plan_.pwm) {
        startPwm(plan_, settings_.dutyPermille);
    } else {
        // The capture yields the bus first: no BDMA request while it runs above the loaded rate.
        if (client_ && !client_->dacStreamStarting()) {
            status_ = DataStatus::Busy;
            return false;
        }
        floatJack();
        TIM6->CR1 = 0;
        TIM6->PSC = plan_.timer.prescaler;
        TIM6->ARR = plan_.timer.reload;
        TIM6->CR2 = TIM_CR2_MMS_1;
        TIM6->EGR = TIM_EGR_UG;
        TIM6->SR = 0;
        DAC2->MCR = 0;
        DAC2->DHR12R1 = tables[activeBank_][0];
        DMAMUX2_Channel0->CCR = BDMA_REQUEST_DAC2_CH1;
        BDMA2_Channel0->CPAR = uint32_t(reinterpret_cast<uintptr_t>(&DAC2->DHR12R1));
        BDMA2_Channel0->CM0AR = uint32_t(reinterpret_cast<uintptr_t>(tables[activeBank_]));
        BDMA2_Channel0->CNDTR = plan_.count;
        BDMA2_Channel0->CCR =
            BDMA_CCR_DIR | BDMA_CCR_CIRC | BDMA_CCR_MINC | BDMA_CCR_PSIZE_0 | BDMA_CCR_MSIZE_0;
        DAC2->SR = DAC_SR_DMAUDR1;
        DAC2->CR = DAC_CR_EN1 | DAC_CR_TEN1 | DAC_CR_TSEL1_0 | DAC_CR_TSEL1_2 | DAC_CR_DMAEN1;
        __DMB();
        BDMA2_Channel0->CCR |= BDMA_CCR_EN;
        Board::delay(1);
        TIM6->CR1 = TIM_CR1_ARPE | TIM_CR1_CEN;
    }
    enabled_ = true;
    status_ = DataStatus::Ok;
    return true;
}
uint32_t Output::timerHz() { return TimerClock::apb1(); }
void Output::stopped(OutputFault fault) {
    fault_ = fault;
    ++faults_;
}
bool Output::enable() {
    if (enabled_)
        return true;
    stopReference(); // The user's output takes the jack and TIM13.
    // A refused ON (Unsupported, Busy, a failed start) keeps the jack quiet until an explicit OFF.
    referenceHeld_ = true;
    if (!configured_ && !configure(settings_)) {
        // Only a setting kept from another clock (a run-mode change, the boot's restore) gets here:
        // configure() refuses a new one before it is kept.
        OutputPlan probe;
        if (settings_.valid() && !OutputPlan::calculate(settings_, TimerClock::apb1(), probe)) {
            status_ = DataStatus::Unsupported;
            stopped(OutputFault::Limit);
        }
        return false;
    }
    if (!lease_.valid() && !resources_.acquire(PeripheralResources::GeneratorDma|PeripheralResources::OutputJack, lease_)) {
        status_ = DataStatus::Busy;
        return false;
    }
    if (apply()) { referenceHeld_ = false; return true; }
    releaseStopped();
    return false;
}
// After a start that did not go through: once this output's own DAC stream is confirmed stopped,
// the jack and its DMA go back (the DMM serial output may take them). A halt whose BDMA would not
// disable keeps them: that stream may still be running.
void Output::releaseStopped() {
    if (!enabled_ && !(BDMA2_Channel0->CCR & BDMA_CCR_EN)) lease_.release();
}
void Output::disable() {
    stopReference();
    if (lease_.valid()) {
        if (!halt())
            return;
        lease_.release();
    }
    status_ = DataStatus::Ok;
    referenceHeld_ = false; // The explicit OFF: the reference may drive the jack again.
}
// The reference: TIM13 PWM on PA6 (AF9), planned exactly at the APB1 timer clock that runs (the
// factory's PSC from the core MHz is exact only where the timer clock is the core's). Not while the
// user's output or its unfinished halt holds the jack, nor when the jack is leased elsewhere.
bool Output::startReference() {
    if (referenceOn_ || enabled_ || lease_.valid()) return referenceOn_;
    OutputPlan plan;
    if (!OutputPlan::calculate(referenceSettings(), TimerClock::apb1(), plan) || !plan.pwm) return false;
    if (!resources_.acquire(PeripheralResources::OutputJack, referenceLease_)) return false;
    startPwm(plan, referenceSettings().dutyPermille);
    referenceOn_ = true;
    referenceHz_ = TimerClock::apb1();
    return true;
}
void Output::stopReference() {
    if (!referenceOn_) return;
    stopPwm();
    floatJack();
    referenceOn_ = false;
    referenceLease_.release();
}
void Output::reference(bool allowed) {
    if (!allowed || enabled_ || referenceHeld_) { stopReference(); return; }
    if (referenceOn_ && referenceHz_ != TimerClock::apb1()) stopReference(); // Planned at another clock.
    if (!referenceOn_) startReference();
}
// A DAC underrun stops the DMA requests for good (the stream must be set up again), a BDMA transfer
// error stops the channel: the output is halted, OFF, HardwareError, and the stop counted once
// (faults(): the UI's toast, SCPI's -240). Never restarted here: the same rate would underrun again,
// and the reference neither (referenceHeld_) until the user's explicit OFF.
void Output::service() {
    if (enabled_ && !plan_.pwm && ((BDMA2->ISR & BDMA_FLAG_TE0) || (DAC2->SR & DAC_SR_DMAUDR1))) {
        const auto fault = (BDMA2->ISR & BDMA_FLAG_TE0) ? OutputFault::Transfer : OutputFault::Underrun;
        if (!halt())
            return;
        lease_.release();
        status_ = DataStatus::HardwareError;
        referenceHeld_ = true;
        stopped(fault);
    }
}
} // namespace openosc::generator
