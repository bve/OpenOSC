#include "openosc/companion/uart.hpp"
#if defined(__arm__)
#include "openosc/companion/uart_samples.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "stm32h7xx_hal.h"

namespace openosc::companion {
namespace {
// Keep PA14's reset low level for ESP deep sleep. PA13 receives ordinary UART polarity.
constexpr uint32_t Tx = GPIO_PIN_14, Rx = GPIO_PIN_13;
constexpr uint32_t TxReset = Tx << 16;
constexpr unsigned ReplyWindowUs = 4800;
constexpr unsigned RxSamples = Baud * UartSamples::PerBit / 1000000 * ReplyWindowUs;
static_assert(Wire::Bytes * CharacterBits * sizeof(uint32_t) <= JsonWorkspace::TextCapacity);
static_assert(Wire::Overhead * CharacterBits * sizeof(uint32_t) + RxSamples <= JsonWorkspace::TextCapacity);
constexpr uint32_t TxEvent = HAL_DMAMUX2_REQ_GEN_LPTIM2_OUT | HAL_DMAMUX_REQ_GEN_RISING | DMAMUX_RGxCR_GE;
constexpr uint32_t RxEvent = HAL_DMAMUX2_REQ_GEN_LPTIM3_OUT | HAL_DMAMUX_REQ_GEN_RISING | DMAMUX_RGxCR_GE;

void resetTimers() {
    // ES0478 §2.12.1: reset LPTIM through RCC when disabling it.
    constexpr uint32_t Timers = RCC_APB4RSTR_LPTIM2RST | RCC_APB4RSTR_LPTIM3RST;
    SET_BIT(RCC->APB4RSTR, Timers);
    CLEAR_BIT(RCC->APB4RSTR, Timers);
}

bool prepare(LPTIM_TypeDef &timer, uint32_t ticks) {
    timer.CR = LPTIM_CR_ENABLE;
    timer.ARR = ticks - 1;
    timer.CMP = ticks / 2;
    constexpr uint32_t Ready = LPTIM_ISR_ARROK | LPTIM_ISR_CMPOK;
    const uint32_t begin = DWT->CYCCNT;
    while ((timer.ISR & Ready) != Ready)
        if (uint32_t(DWT->CYCCNT - begin) >= SystemCoreClock / 40000u) return false;
    return true;
}
}

bool Uart::probeOwnsClock() const { return GPIOA->IDR & Tx; }
bool Uart::debugAllowed() { return (FLASH->OPTSR_CUR & FLASH_OPTSR_RDP) == OB_RDP_LEVEL_0; }

bool Uart::open() {
    if (owner_ == this) return true;
    if (owner_ || (CoreDebug->DHCSR & CoreDebug_DHCSR_C_DEBUGEN_Msk)) return false;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    if (probeOwnsClock()) return false;
    __HAL_RCC_LPTIM2_CONFIG(RCC_LPTIM2CLKSOURCE_PLL3);
    __HAL_RCC_LPTIM3_CONFIG(RCC_LPTIM3CLKSOURCE_PLL3);
    if (!__HAL_RCC_GET_FLAG(RCC_FLAG_PLL3RDY)) return false;
    PLL3_ClocksTypeDef pll3{};
    HAL_RCCEx_GetPLL3ClockFreq(&pll3);
    clockHz_ = pll3.PLL3_R_Frequency;
    if (clockHz_ < Baud * UartSamples::PerBit * 4 ||
        clockHz_ % (Baud * UartSamples::PerBit)) return false;
    __HAL_RCC_LPTIM2_CLK_ENABLE();
    __HAL_RCC_LPTIM3_CLK_ENABLE();
    __HAL_RCC_BDMA2_CLK_ENABLE();
    resetTimers();
    owner_ = this;
    wake(false);
    GPIO_InitTypeDef pin{};
    pin.Pin = Tx; pin.Mode = GPIO_MODE_OUTPUT_PP; pin.Pull = GPIO_PULLDOWN; pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &pin);
    pin.Pin = Rx; pin.Mode = GPIO_MODE_INPUT; pin.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &pin);
    return true;
}

void Uart::close() {
    if (owner_ != this) return;
    yieldBus();
    GPIO_InitTypeDef pin{};
    pin.Mode = GPIO_MODE_AF_PP; pin.Alternate = GPIO_AF0_SWJ; pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Pin = Rx; pin.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &pin);
    pin.Pin = Tx; pin.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &pin);
    __HAL_RCC_LPTIM2_CLK_DISABLE();
    __HAL_RCC_LPTIM3_CLK_DISABLE();
    owner_ = nullptr;
    result_ = Result::Idle;
}

void Uart::wake(bool high) {
    GPIOA->BSRR = high ? Tx : TxReset;
    wakeHigh_ = high;
}

bool Uart::start(const uint8_t *data, unsigned size, bool collectReply) {
    if (owner_ != this || result_ != Result::Idle || !size || size > Wire::Bytes ||
        (collectReply && size != Wire::Overhead)) return false;
    scratch_ = JsonWorkspace::borrowText(yieldBus);
    if (!scratch_) return false;

    // BSRR writes only the companion bit. Each byte starts high and ends in two low bits.
    txWords_ = 0;
    const auto bit = [&](bool high) {
        const uint32_t word = high ? Tx : TxReset;
        std::memcpy(scratch_ + txWords_++ * sizeof(word), &word, sizeof(word));
    };
    for (unsigned i = 0; i < size; ++i) {
        bit(true);
        for (unsigned j = 0; j < 8; ++j) bit(!(data[i] & (1u << j)));
        for (unsigned j = 0; j < StopBits; ++j) bit(false);
    }

    receiving_ = collectReply;
    const uint32_t ticks = clockHz_ / Baud;
    if (!prepare(*LPTIM2, ticks) || (receiving_ && !prepare(*LPTIM3, ticks / UartSamples::PerBit))) {
        finish(Result::Fault);
        return true;
    }
    BDMA2->IFCR = BDMA_IFCR_CTEIF1 | BDMA_IFCR_CTCIF1 | BDMA_IFCR_CHTIF1 |
                  BDMA_IFCR_CTEIF2 | BDMA_IFCR_CTCIF2 | BDMA_IFCR_CHTIF2;
    DMAMUX2_RequestGenStatus->RGCFR = DMAMUX_RGCFR_COF0 | DMAMUX_RGCFR_COF1;
    DMAMUX2_Channel1->CCR = BDMA_REQUEST_GENERATOR0;
    BDMA2_Channel1->CPAR = reinterpret_cast<uint32_t>(&GPIOA->BSRR);
    BDMA2_Channel1->CM0AR = reinterpret_cast<uint32_t>(scratch_);
    BDMA2_Channel1->CNDTR = txWords_;
    BDMA2_Channel1->CCR = BDMA_CCR_DIR | BDMA_CCR_MINC | BDMA_CCR_PSIZE_1 | BDMA_CCR_MSIZE_1;
    if (receiving_) {
        DMAMUX2_Channel2->CCR = BDMA_REQUEST_GENERATOR1;
        BDMA2_Channel2->CPAR = reinterpret_cast<uint32_t>(&GPIOA->IDR) + 1u;
        BDMA2_Channel2->CM0AR = reinterpret_cast<uint32_t>(scratch_ + txWords_ * sizeof(uint32_t));
        BDMA2_Channel2->CNDTR = RxSamples;
        BDMA2_Channel2->CCR = BDMA_CCR_MINC;
    }
    __DMB();
    result_ = Result::Busy;
    started_ = DWT->CYCCNT;
    if (receiving_) {
        BDMA2_Channel2->CCR |= BDMA_CCR_EN;
        DMAMUX2_RequestGenerator1->RGCR = RxEvent;
        LPTIM3->CR = LPTIM_CR_ENABLE | LPTIM_CR_CNTSTRT;
    }
    BDMA2_Channel1->CCR |= BDMA_CCR_EN;
    DMAMUX2_RequestGenerator0->RGCR = TxEvent;
    LPTIM2->CR = LPTIM_CR_ENABLE | LPTIM_CR_CNTSTRT;
    return true;
}

void Uart::stopHardware() {
    // Withdraw both request sources before returning the SRAM4 loan or arming the ADC.
    DMAMUX2_RequestGenerator0->RGCR = 0;
    DMAMUX2_RequestGenerator1->RGCR = 0;
    resetTimers();
    BDMA2_Channel1->CCR = 0;
    BDMA2_Channel2->CCR = 0;
    wake(false);
    __DSB();
}

void Uart::finish(Result result) {
    stopHardware();
    JsonWorkspace::returnText();
    scratch_ = nullptr;
    result_ = result;
}

void Uart::collect() {
    if (result_ != Result::Busy) return;
    constexpr uint32_t Errors = BDMA_ISR_TEIF1 | BDMA_ISR_TEIF2;
    const uint32_t flags = BDMA2->ISR;
    if (flags & Errors) finish(Result::Fault);
    else if ((flags & BDMA_ISR_TCIF1) && (!receiving_ || (flags & BDMA_ISR_TCIF2))) {
        reply_.size = 0;
        if (receiving_)
            UartSamples::decode(reinterpret_cast<const uint8_t *>(scratch_ + txWords_ * sizeof(uint32_t)),
                                RxSamples, Rx >> 8, reply_);
        finish(Result::Done);
    } else if (uint32_t(DWT->CYCCNT - started_) >= SystemCoreClock / 100u) finish(Result::Fault);
}

void Uart::yieldBus() {
    if (!owner_) return;
    // DMA can have completed while LVGL was waiting. Preserve that reply before returning SRAM4.
    owner_->collect();
    if (owner_->result_ == Result::Busy) owner_->finish(Result::Interrupted);
    if (owner_->wakeHigh_) owner_->wake(false);
}

Uart::Result Uart::poll(Frame &reply) {
    collect();
    const auto result = result_;
    if (result != Result::Busy) {
        if (result == Result::Done && receiving_) reply = reply_;
        result_ = Result::Idle;
    }
    return result;
}
}
#endif
