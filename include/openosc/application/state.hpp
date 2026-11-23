#pragma once
#include <cstdint>
#include "openosc/instrument/mode_policy.hpp"
#include "openosc/acquisition/readouts.hpp"

namespace openosc {
struct ScopeSettings {
    uint64_t timebaseNs = 1000000;
    uint32_t millivoltsPerDivision[2] = {500, 500};
    scope::ReadoutSelection measurements;
    scope::MeterReadoutSelection meterMeasurements;
};
struct MeterSettings {
    unsigned function = 0;
};
struct ApplicationState {
    MeasurementMode mode = MeasurementMode::Oscilloscope;
    ScopeSettings scope;
    MeterSettings meter;
    bool settingsOpen = false;
    void toggleMode() {
        mode = nextMode(mode);
        settingsOpen = false;
    }
};
}
