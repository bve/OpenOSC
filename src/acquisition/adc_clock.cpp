#include "openosc/acquisition/adc_clock.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/usb_trace.hpp"
#include <algorithm>

namespace openosc::scope {
namespace {
void clockPin(GPIO_TypeDef *port,uint32_t pin) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin=pin; gpio.Mode=GPIO_MODE_AF_PP;
    gpio.Speed=GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate=GPIO_AF1_TIM1;
    HAL_GPIO_Init(port,&gpio);
}

// Sixteen ADC periods at the plan's rate (the converters' pipeline), at most 1 ms, at least 2 us.
void settle(uint16_t prescaler, uint16_t period) {
    const uint64_t perPeriod = uint64_t(prescaler + 1u) * period;
    const uint32_t cycles = uint32_t(std::clamp<uint64_t>(16 * perPeriod, SystemCoreClock / 500000, SystemCoreClock / 1000));
    const uint32_t start = DWT->CYCCNT;
    while (uint32_t(DWT->CYCCNT - start) < cycles) __NOP();
}
}
// Both ADC clocks (CH1 and CH3) enabled: in phase with each other at the factory polarity (CC1P/CC3P
// set) or both inverted (AdcReadPhase); interleaved, CH1 at the high polarity and CH3 at the low one.
uint32_t AdcClock::outputs(uint32_t timerHz, uint16_t prescaler, uint16_t period, bool interleaved) {
    if (interleaved) return TIM_CCER_CC1E | TIM_CCER_CC3E | TIM_CCER_CC3P;
    const uint32_t inverted =
        AdcReadPhase::select(timerHz, prescaler, period) == AdcReadPhase::Phase::Factory ? TIM_CCER_CC1P | TIM_CCER_CC3P : 0;
    return TIM_CCER_CC1E | TIM_CCER_CC3E | inverted;
}
void AdcClock::start(uint16_t prescaler, uint16_t period, uint32_t timerHz, bool interleaved) {
    if (running_ || period < 2) return;
    __HAL_RCC_TIM1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    TIM1->CR1=0; TIM1->DIER=0; TIM1->SMCR=0;
    TIM1->CR2=TIM_CR2_MMS_1;
    TIM1->PSC=prescaler; TIM1->ARR=period-1u; TIM1->CCR1=TIM1->CCR3=compare(period, interleaved);
    TIM1->CCMR1=(6U<<4)|TIM_CCMR1_OC1PE;
    TIM1->CCMR2=(6U<<4)|TIM_CCMR2_OC3PE;
    TIM1->CCER=outputs(timerHz, prescaler, period, interleaved);
    TIM1->BDTR=TIM_BDTR_MOE;
    TIM1->EGR=TIM_EGR_UG; TIM1->CNT=0; TIM1->SR=0;
    clockPin(GPIOE,GPIO_PIN_9); clockPin(GPIOA,GPIO_PIN_10);
    TIM1->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
    settle(prescaler, period);
    running_=true;
    usbtrace::mark(usbtrace::Tag::AdcStart);
}
void AdcClock::configure(uint16_t prescaler, uint16_t period, uint32_t timerHz, bool interleaved) {
    const uint32_t ccer = outputs(timerHz, prescaler, period, interleaved);
    const uint16_t edge = compare(period, interleaved);
    if (!running_ || period < 2 ||
        (TIM1->PSC == prescaler && TIM1->ARR == uint32_t(period - 1) && TIM1->CCER == ccer &&
         TIM1->CCR1 == edge && TIM1->CCR3 == edge))
        return; // An unchanged plan keeps the running clock (no restart between records).
    TIM1->CCER = ccer;
    TIM1->CCR1 = TIM1->CCR3 = edge;                  // Preloaded: applied with PSC/ARR at the update.
    TIM1->PSC = prescaler; TIM1->ARR = period - 1u;
    TIM1->EGR = TIM_EGR_UG;                          // Now; TIM3 does not take TRGO yet (SMCR 0).
    settle(prescaler, period);
}
void AdcClock::stop() {
    if (!running_) return;
    TIM1->DIER=0; TIM1->CR1 &= ~TIM_CR1_CEN;
    running_=false;
    usbtrace::mark(usbtrace::Tag::AdcStop);
}
}
