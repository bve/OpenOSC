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
    // Counts every attempt to retime SYSCLK (startup, a profile change, its rollback or HSI fallback,
    // Remote, shutdown): a reader of activeMode() asks again only when this moved, so no RCC read
    // runs on every display pass (an SRD access the capture shares, GitHub #33). RAM only.
    static uint32_t changes() { return changes_; }
    static inline uint32_t changes_ = 0;
};
}
