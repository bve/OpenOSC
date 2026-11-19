#include "openosc/platform/board.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/display/display.hpp"

namespace openosc {
namespace {
LTDC_HandleTypeDef lcd;
void pins(GPIO_TypeDef* port, uint32_t mask, uint32_t af) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin = mask; gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate = af;
    HAL_GPIO_Init(port, &gpio);
}
}
bool Display::initialize(bool keep) {
    __HAL_RCC_LTDC_CLK_ENABLE();
    keep = keep && (LTDC->GCR & LTDC_GCR_LTDCEN) && LTDC_Layer1->CFBAR == 0x24000000U; // Still scanning it, else set up.
    if (!keep) { __HAL_RCC_LTDC_FORCE_RESET(); __HAL_RCC_LTDC_RELEASE_RESET(); }
#ifndef OPENOSC_APPLICATION
    RCC_PeriphCLKInitTypeDef clock{};
    clock.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    // 25 MHz / 25 * 400 / 20 = 20 MHz, wide VCO at 400 MHz.
    clock.PLL3.PLL3M = 25; clock.PLL3.PLL3N = 400;
    clock.PLL3.PLL3P = 2; clock.PLL3.PLL3Q = 2; clock.PLL3.PLL3R = 20;
    clock.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_0;
    clock.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    if (HAL_RCCEx_PeriphCLKConfig(&clock) != HAL_OK) return false;
#endif
    pins(GPIOA, GPIO_PIN_3 | GPIO_PIN_5 | GPIO_PIN_9, GPIO_AF14_LTDC);
    pins(GPIOA, GPIO_PIN_8, GPIO_AF13_LTDC);
    pins(GPIOB, GPIO_PIN_0 | GPIO_PIN_1, GPIO_AF9_LTDC);
    pins(GPIOB, GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_15, GPIO_AF14_LTDC);
    pins(GPIOC, GPIO_PIN_0, GPIO_AF11_LTDC);
    pins(GPIOC, GPIO_PIN_7, GPIO_AF14_LTDC);
    pins(GPIOE, GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15, GPIO_AF14_LTDC);
    GPIO_InitTypeDef gpio{};
    gpio.Pin = GPIO_PIN_13; gpio.Mode = GPIO_MODE_OUTPUT_PP;
    if (!keep) { // The panel's reset blanks it: not while the logo stays up.
        HAL_GPIO_Init(GPIOC, &gpio);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        Board::delay(50);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        Board::delay(10);
    }
    if (!keep) {
        if (!configure(0x24000000)) return false;
        LTDC_Layer2->CR = 0;
        clear(); present();
    }
    PowerControl::backlight(true);
    return true;
}
// The controller's timings and layer, scanning `frame`.
bool Display::configure(uint32_t frame) {
    lcd = {};
    lcd.Instance = LTDC;
    lcd.Init.HSPolarity = LTDC_HSPOLARITY_AL;
    lcd.Init.VSPolarity = LTDC_VSPOLARITY_AL;
    lcd.Init.DEPolarity = LTDC_DEPOLARITY_AL;
    lcd.Init.PCPolarity = LTDC_PCPOLARITY_IPC;
    lcd.Init.HorizontalSync = 4; lcd.Init.VerticalSync = 2;
    lcd.Init.AccumulatedHBP = 118; lcd.Init.AccumulatedVBP = 17;
    lcd.Init.AccumulatedActiveW = 438; lcd.Init.AccumulatedActiveH = 257;
    lcd.Init.TotalWidth = 498; lcd.Init.TotalHeigh = 261;
    if (HAL_LTDC_Init(&lcd) != HAL_OK) return false;
    LTDC_LayerCfgTypeDef layer{};
    layer.WindowX1 = Width; layer.WindowY1 = Height;
    layer.PixelFormat = LTDC_PIXEL_FORMAT_RGB565; layer.Alpha = 255;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_CA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_CA;
    layer.FBStartAdress = frame; layer.ImageWidth = Width; layer.ImageHeight = Height;
    return HAL_LTDC_ConfigLayer(&lcd, &layer, 0) == HAL_OK;
}
bool Display::restart(const void *frame) {
    // A peripheral reset stops every read of the controller at once; then the same setup.
    __HAL_RCC_LTDC_FORCE_RESET(); __HAL_RCC_LTDC_RELEASE_RESET();
    if (!configure(uint32_t(reinterpret_cast<uintptr_t>(frame)))) return false;
    LTDC_Layer2->CR = 0;
    return (LTDC->GCR & LTDC_GCR_LTDCEN) != 0;
}
}
