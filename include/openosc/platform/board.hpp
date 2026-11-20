#pragma once
#include <cstdint>
#include "stm32h7xx_hal.h"
#include "openosc/platform/diagnostics.hpp"
namespace openosc {
class Board {
public:
    static bool initialize(bool keepBootDisplay = false);
    static void service();
    static uint32_t milliseconds();
    static void delay(uint32_t ms);
};
}
