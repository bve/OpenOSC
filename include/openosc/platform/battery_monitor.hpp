#pragma once
#include "openosc/power/battery.hpp"
#include "stm32h7xx_hal.h"

namespace openosc {
class BatteryMonitor {
public:
    // The status lines (PC9, charge status) become inputs first, so they are read also when the
    // ADC set-up fails (they would otherwise stay in their analog reset mode and read 0).
    bool initialize();
    void poll(uint32_t now);
    const power::BatteryState &state() const { return gauge_.state(); }
    void trim(int gainPermille, int offsetMv) { gauge_.trim(gainPermille, offsetMv); } // Display only (BatteryGauge::trim).
private:
    ADC_HandleTypeDef adc_{};
    power::BatteryGauge gauge_;
    uint32_t next_=0,started_=0;
    bool ready_=false,pending_=false;
};
} // namespace openosc
