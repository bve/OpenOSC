#include "openosc/platform/board.hpp"
#include "openosc/platform/clocks.hpp"
namespace openosc {
namespace {
// PLL3 (USB 48 MHz, LCD 20 MHz). DS13196: the wide VCO (128..560 MHz) is characterised for a
// 2..16 MHz reference; HSE/5 = 5 MHz sits in RGE2 (4..8 MHz).
struct Pll3Plan { uint32_t m, n, q, r; };
constexpr Pll3Plan Pll3{5, 96, 10, 24};
constexpr uint32_t Pll3Reference = HSE_VALUE / Pll3.m, Pll3Vco = Pll3Reference * Pll3.n;
static_assert(HSE_VALUE % Pll3.m == 0 && Pll3Reference >= 4000000 && Pll3Reference <= 8000000, "PLL3 reference outside RGE2");
static_assert(Pll3Vco >= 128000000 && Pll3Vco <= 560000000, "PLL3 wide VCO range");
static_assert(Pll3Vco / Pll3.q == 48000000 && Pll3Vco % Pll3.q == 0, "USB needs exactly 48 MHz");
static_assert(Pll3Vco / Pll3.r == 20000000 && Pll3Vco % Pll3.r == 0, "LCD pixel clock 20 MHz");
}
bool ClockControl::configure(bool keepBootDisplay) {
    HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);
    uint32_t start = HAL_GetTick();
    while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {
        Board::service();
        if (HAL_GetTick() - start > 100) return false;
    }
    RCC_OscInitTypeDef oscillator{};
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI | RCC_OSCILLATORTYPE_HSE
        | RCC_OSCILLATORTYPE_HSI48;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.HSEState = RCC_HSE_ON;
    oscillator.HSI48State = RCC_HSI48_ON;
    oscillator.PLL.PLLState = RCC_PLL_NONE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) return false;
    if (!selectPerformance(0)) return false;
#ifdef OPENOSC_APPLICATION
    // The second stage has already configured PLL3 if it handed us a live logo.
    if (!keepBootDisplay && !configureDisplayClock()) return false;
#endif
    return true;
}
bool ClockControl::configureDisplayClock() {
    RCC_PeriphCLKInitTypeDef peripheral{};
    peripheral.PeriphClockSelection = RCC_PERIPHCLK_LTDC | RCC_PERIPHCLK_USB;
    // Independent PLL3 outputs keep USB at48 MHz and LCD at20 MHz in both
    // acquisition profiles; neither is derived from the retimed PLL1.
    peripheral.PLL3.PLL3M = Pll3.m; peripheral.PLL3.PLL3N = Pll3.n;
    peripheral.PLL3.PLL3P = 2; peripheral.PLL3.PLL3Q = Pll3.q; peripheral.PLL3.PLL3R = Pll3.r;
    peripheral.PLL3.PLL3RGE = RCC_PLL3VCIRANGE_2;
    peripheral.PLL3.PLL3VCOSEL = RCC_PLL3VCOWIDE;
    peripheral.UsbClockSelection = RCC_USBCLKSOURCE_PLL3;
    return HAL_RCCEx_PeriphCLKConfig(&peripheral) == HAL_OK;
}
uint32_t ClockControl::activeMode() {
    if (__HAL_RCC_GET_SYSCLK_SOURCE() != RCC_SYSCLKSOURCE_STATUS_PLLCLK) return RunModeCount;
    for (uint32_t mode = 0; mode < RunModeCount; ++mode)
        if (RunModes[mode].coreHz == SystemCoreClock) return mode;
    return RunModeCount;
}
bool ClockControl::selectPerformance(uint32_t mode) {
    if (mode >= RunModeCount) return false;
    const auto &profile = RunModes[mode];
    const uint32_t target = profile.coreHz;
    if (SystemCoreClock == target && __HAL_RCC_GET_SYSCLK_SOURCE()==RCC_SYSCLKSOURCE_STATUS_PLLCLK)
        return true;
    RCC_ClkInitTypeDef bus{};
    bus.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_PCLK1
        | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_D3PCLK1;
    bus.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
    bus.SYSCLKDivider = RCC_SYSCLK_DIV1;
    bus.AHBCLKDivider = RCC_HCLK_DIV1;
    bus.APB1CLKDivider = RCC_APB1_DIV2;
    bus.APB2CLKDivider = RCC_APB2_DIV2;
    bus.APB3CLKDivider = RCC_APB3_DIV2;
    bus.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&bus, FLASH_LATENCY_5) != HAL_OK) return false;
    RCC_OscInitTypeDef oscillator{};
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 5;
    oscillator.PLL.PLLN = profile.pllN;
    oscillator.PLL.PLLP = 2;
    oscillator.PLL.PLLQ = 8;
    oscillator.PLL.PLLR = 2;
    oscillator.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
    oscillator.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
    if (HAL_RCC_OscConfig(&oscillator) != HAL_OK) return false;
    bus.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    // The profile's wait states (RunMode::flashLatency; FLASH_LATENCY_n == n).
    static_assert(FLASH_LATENCY_5 == 5 && FLASH_LATENCY_7 == 7 && FLASH_LATENCY_8 == 8, "FLASH_LATENCY_n is n");
    if (HAL_RCC_ClockConfig(&bus, profile.flashLatency) != HAL_OK) return false;
    return SystemCoreClock == target;
}
}
