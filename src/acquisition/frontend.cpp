#include "openosc/acquisition/frontend.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/acquisition/trigger_reference.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/control_latch.hpp"
#include "openosc/platform/usb_trace.hpp"

namespace openosc::scope {
namespace {
constexpr uint8_t codes[2][11] = {{3, 0, 6, 5, 7, 1, 3, 0, 6, 5, 7}, {1, 0, 5, 7, 6, 3, 1, 0, 5, 7, 6}};
void output(GPIO_TypeDef *port, uint32_t pins) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}
void alternate(GPIO_TypeDef *port, uint32_t pins, uint32_t af) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
}
} // namespace
void AnalogFrontend::initialize() {
    if (initialized_)
        return;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    GPIOE->BSRR = uint32_t(GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_7) << 16;
    output(GPIOE, GPIO_PIN_1 | GPIO_PIN_3 | GPIO_PIN_7 | GPIO_PIN_8);
    output(GPIOC, GPIO_PIN_12);
    output(GPIOB, GPIO_PIN_5);
    __HAL_RCC_TIM8_CLK_ENABLE();
    TIM8->CR1 = 0;
    TIM8->DIER = 0; // PWM only; TIM14 owns the shared trigger/commutation IRQ.
    TIM8->PSC = 0;
    TIM8->ARR = 9999;
    TIM8->CCR1 = 5000;
    TIM8->CCR3 = 5000;
    TIM8->CCMR1 = (6U << 4) | TIM_CCMR1_OC1PE;
    TIM8->CCMR2 = (6U << 4) | TIM_CCMR2_OC3PE;
    TIM8->CCER = TIM_CCER_CC1NE | TIM_CCER_CC3E;
    TIM8->BDTR = TIM_BDTR_MOE;
    TIM8->EGR = TIM_EGR_UG;
    TIM8->CR1 = TIM_CR1_CEN | TIM_CR1_ARPE;
    alternate(GPIOA, GPIO_PIN_7, GPIO_AF3_TIM8);
    alternate(GPIOC, GPIO_PIN_8, GPIO_AF3_TIM8);
    initialized_ = true;
}
bool AnalogFrontend::scope(const FrontendSettings &settings,int16_t triggerLevel) {
    // From the multimeter or power-up the whole path changes; so does a new offset compare
    // (a hardware-zero profile, a calibration trial) under the same settings.
    bool analog = forced_ || !scopeActive_ || calibration_.generation() != generation_ ||
                  settings.interleave != previous_.interleave || settings.rangeBoost != previous_.rangeBoost;
    for (unsigned c = 0; c < 2; ++c)
        analog = analog || settings.range[c] != previous_.range[c] || settings.position[c] != previous_.position[c] ||
                 settings.coupling[c] != previous_.coupling[c] || settings.enabled[c] != previous_.enabled[c];
    if (!analog && triggerLevel == previousLevel_) {
        ++applies_;
        return true;
    }
    usbtrace::mark(usbtrace::Tag::FrontendBegin);
    uint16_t compare[2];
    if (!settings.consistent()) return false;
    for (unsigned c = 0; c < 2; ++c)
        if (settings.range[c] > 10 || static_cast<unsigned>(settings.coupling[c]) > 1 ||
            !calibration_.offsetCompare(c, settings.physicalRange(c), settings.position[c], compare[c]))
            return false;
    if (triggerLevel < -128 || triggerLevel > 127)
        return false;
    initialize();
    // Common power/control bits (bit 10 also the factory selector's, 0804B878); enables, range bits
    // and the interleave selector (bits 8 and 11 together) are committed together.
    uint16_t word = 0x1600 | (settings.interleave ? InterleaveBits : 0);
    for (unsigned c = 0; c < 2; ++c)
        if (settings.enabled[c]) {
            const unsigned range = settings.physicalRange(c);
            word |= uint16_t(codes[c][range]) << (c * 3);
            if (range < 5)
                word |= 1U << (6 + c);
            word |= 1U << (14 + c);
        }
    ControlLatch::write(word);
    usbtrace::mark(usbtrace::Tag::Latched);
    GPIOE->BSRR = (settings.enabled[0] || settings.enabled[1]) ? GPIO_PIN_8 : uint32_t(GPIO_PIN_8) << 16;
    GPIOC->BSRR = settings.coupling[0] == Coupling::Dc ? GPIO_PIN_12 : uint32_t(GPIO_PIN_12) << 16;
    GPIOB->BSRR = settings.coupling[1] == Coupling::Dc ? GPIO_PIN_5 : uint32_t(GPIO_PIN_5) << 16;
    TIM8->CCR1 = compare[0];
    TIM8->CCR3 = compare[1];
    usbtrace::mark(usbtrace::Tag::Offset);
    if (!referenceReady_) {
        __HAL_RCC_DAC12_CLK_ENABLE();
        GPIO_InitTypeDef reference{};
        reference.Pin = GPIO_PIN_4;
        reference.Mode = GPIO_MODE_ANALOG;
        HAL_GPIO_Init(GPIOA, &reference);
        DAC1->CR &= 0xffff0000;
        DAC1->MCR &= ~DAC_MCR_MODE1;
    }
    DAC1->DHR12R1 = TriggerReference::dacCode(triggerLevel);
    if (!referenceReady_) {
        DAC1->CR |= DAC_CR_EN1;
        referenceReady_ = true;
    }
    usbtrace::mark(usbtrace::Tag::Reference);
    previous_ = settings;
    previousLevel_ = triggerLevel;
    generation_ = calibration_.generation();
    scopeActive_ = true;
    if (analog) { changedAt_ = Board::milliseconds(); settling_ = true; }
    forced_ = false;
    ++applies_;
    usbtrace::mark(usbtrace::Tag::FrontendEnd);
    return true;
}
void AnalogFrontend::multimeterOff() {
    initialize();
    ControlLatch::write(0x1600); // The scope word with both channels off.
    GPIOE->BSRR = uint32_t(GPIO_PIN_8) << 16;
    scopeActive_ = false;
}
void AnalogFrontend::multimeter(uint16_t generatorBits) {
    initialize();
    ControlLatch::write(0x3200 | generatorBits);
    GPIOE->BSRR = uint32_t(GPIO_PIN_8) << 16;
    scopeActive_ = false;
}
} // namespace openosc::scope
