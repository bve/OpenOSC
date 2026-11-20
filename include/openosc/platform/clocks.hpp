#pragma once
#include "openosc/settings/run_mode.hpp"
namespace openosc {
class ClockControl {
public:
    static bool configure(bool keepBootDisplay);
    static bool configureDisplayClock();
    // PLL1 retimed through HSI (VOS0 kept; flash wait states and SysTick follow HAL_RCC_ClockConfig).
    // At startup, or at run time with every PLL1-timed user stopped first (RunModeSwitch). USB
    // (HSI48), LTDC (PLL3), NOR SPI and the battery ADC (HSI) do not depend on PLL1.
    static bool selectPerformance(uint32_t mode);
    // The profile the core actually runs (SYSCLK from PLL1 at its frequency); RunModeCount if none
    // (the HSI fallback left by a failed change).
    static uint32_t activeMode();
};
}
