#pragma once
#include <cstdint>
#include "stm32h7xx_hal.h"
#include "openosc/platform/diagnostics.hpp"
namespace openosc {
class Board {
public:
    static bool initialize();
    static void service();
    static uint32_t milliseconds();
    static void delay(uint32_t ms);
    // Application: frame 0 held the factory logo and is scanned again on this image's set-up (true once).
    static bool splash();
    static constexpr unsigned DisplayEntryWords = 7;
    static const uint32_t *displayAtEntry(); // Logo (1 found, 3 restart failed), LTDC GCR, L1 CR CFBAR PFCR CFBLR, L2 CR at entry (SYST:BOOT?).
};
}
