#ifdef OPENOSC_APPLICATION
#include "openosc/platform/mcu_temperature.hpp"
#include "openosc/platform/board.hpp"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_ll_adc.h"

namespace openosc {
namespace {
constexpr uint32_t PeriodMs = 1000;
constexpr uint32_t TimeoutMs = 25;
constexpr uint32_t CleanupTimeoutMs = 5;
constexpr uint32_t InternalInputs = ADC_CCR_TSEN | ADC_CCR_VREFEN;
constexpr uint32_t InjectedFlags = ADC_ISR_JEOC | ADC_ISR_JEOS;

std::optional<int16_t> temperatureReading() {
    const uint32_t sensor = ADC2->JDR1;
    const uint32_t reference = ADC2->JDR2;
    const uint32_t factoryRef = *VREFINT_CAL_ADDR;
    const int32_t calLow = *TEMPSENSOR_CAL1_ADDR, calHigh = *TEMPSENSOR_CAL2_ADDR;
    if (!sensor || sensor >= UINT16_MAX || !reference || reference >= UINT16_MAX ||
        !factoryRef || factoryRef == UINT16_MAX || !calLow || calHigh == UINT16_MAX || calHigh <= calLow) return {};

    // Normalize the 16-bit sample to the factory calibration's VREF+ using VREFINT.
    const uint32_t normalized = (sensor * factoryRef + reference / 2) / reference;
    if (normalized > UINT16_MAX) return {};
    const int32_t span = calHigh - calLow;
    const int32_t delta = (int32_t(normalized) - calLow) * (TEMPSENSOR_CAL2_TEMP - TEMPSENSOR_CAL1_TEMP) * 10;
    const int32_t rounded = delta + (delta < 0 ? -span / 2 : span / 2);
    const int32_t deciC = TEMPSENSOR_CAL1_TEMP * 10 + rounded / span;
    if (deciC < -550 || deciC > 1500) return {};
    return int16_t(deciC);
}

void startAdc() {
    // ADC12 takes its asynchronous clock from HSI via BatteryMonitor. Leave ADC1 and the
    // common clock configuration alone. The two internal channels are exclusive to ADC2.
    SET_BIT(ADC12_COMMON->CCR, InternalInputs);
    CLEAR_BIT(ADC2->CR, ADC_CR_DEEPPWD);
    ADC2->CR = ADC_CR_ADVREGEN | (ADC1->CR & ADC_CR_BOOST);
    ADC2->CFGR = ADC_CFGR_JQDIS; // 16-bit, software trigger, one injected sequence.
    ADC2->SMPR2 = ADC_SMPR2_SMP18 | ADC_SMPR2_SMP19; // 387.5 cycles on each internal input.
    ADC2->PCSEL = ADC_PCSEL_PCSEL_18 | ADC_PCSEL_PCSEL_19;
    ADC2->JSQR = 1U | (18U << ADC_JSQR_JSQ1_Pos) | (19U << ADC_JSQR_JSQ2_Pos);
    // Average 16 conversions per channel while retaining the factory calibration's 16-bit scale.
    ADC2->CFGR2 = ADC_CFGR2_JOVSE | (15U << ADC_CFGR2_OVSR_Pos) | (4U << ADC_CFGR2_OVSS_Pos);
}
} // namespace

std::optional<int16_t> McuTemperature::captureStartup() {
    const uint32_t start = Board::milliseconds();
    do {
        poll(Board::milliseconds(), true);
        if (state_ == State::Stopping || state_ == State::Idle) break;
        Board::service();
    } while (Board::milliseconds() - start < TimeoutMs);

    const auto reading = reading_;
    const uint32_t cleanupStart = Board::milliseconds();
    do {
        poll(Board::milliseconds(), false);
        if (state_ == State::Off) break;
        Board::service();
    } while (Board::milliseconds() - cleanupStart < CleanupTimeoutMs);
    return reading;
}

bool McuTemperature::stop() {
    const uint32_t control = ADC2->CR;
    if (control & (ADC_CR_ADCAL | ADC_CR_ADDIS | ADC_CR_JADSTP)) return false;
    if (control & ADC_CR_JADSTART) {
        SET_BIT(ADC2->CR, ADC_CR_JADSTP);
        return false;
    }
    if (control & ADC_CR_ADEN) {
        SET_BIT(ADC2->CR, ADC_CR_ADDIS);
        return false;
    }
    if (ADC1->CR & ADC_CR_ADEN) return false; // Both ADCs must be disabled to change CCR.
    CLEAR_BIT(ADC2->CR, ADC_CR_ADVREGEN);
    SET_BIT(ADC2->CR, ADC_CR_DEEPPWD);
    CLEAR_BIT(ADC12_COMMON->CCR, InternalInputs);
    return true;
}

std::optional<int16_t> McuTemperature::poll(uint32_t now, bool enabled) {
    if (!enabled) {
        reading_.reset();
        if (state_ == State::Off || state_ == State::Idle || stop()) state_ = State::Off;
        else state_ = State::Stopping;
        return {};
    }
    const uint32_t elapsed = now - startedAt_;
    if (elapsed >= PeriodMs + TimeoutMs) reading_.reset();
    switch (state_) {
    case State::Off: case State::Idle:
        if (state_ == State::Idle && elapsed < PeriodMs) break;
        if (__HAL_RCC_ADC12_IS_CLK_ENABLED() &&
            (ADC12_COMMON->CCR & (ADC_CCR_CKMODE | ADC_CCR_PRESC)) == ADC_CLOCK_ASYNC_DIV4) {
            if (ADC1->CR & ADC_CR_ADEN) break;
            startAdc();
            state_ = State::Starting;
        } else {
            reading_.reset();
            state_ = State::Idle;
        }
        startedAt_ = now;
        break;
    case State::Starting:
        // A two-tick wait guarantees at least 1 ms for the regulator and internal inputs.
        if (elapsed >= 2) {
            SET_BIT(ADC2->CR, ADC_CR_ADCAL);
            state_ = State::Calibrating;
        }
        break;
    case State::Calibrating:
        if (!(ADC2->CR & ADC_CR_ADCAL)) {
            ADC2->ISR = ADC_ISR_ADRDY | InjectedFlags;
            SET_BIT(ADC2->CR, ADC_CR_ADEN);
            state_ = State::Enabling;
        }
        break;
    case State::Enabling:
        if (ADC2->ISR & ADC_ISR_ADRDY) {
            SET_BIT(ADC2->CR, ADC_CR_JADSTART);
            state_ = State::Measuring;
        }
        break;
    case State::Measuring:
        if (ADC2->ISR & ADC_ISR_JEOS) {
            reading_ = temperatureReading();
            state_ = State::Stopping;
        }
        break;
    case State::Stopping:
        if (stop()) state_ = State::Idle;
        break;
    }
    if (state_ >= State::Starting && state_ <= State::Measuring && now - startedAt_ >= TimeoutMs) {
        reading_.reset();
        state_ = State::Stopping;
    }
    return reading_;
}
} // namespace openosc
#endif
