#pragma once
#include "openosc/measurement/dmm/session.hpp"
#include "openosc/settings/meter_appearance.hpp"

namespace openosc::ui {
struct MeterContext {
    const dmm::Statistics *statistics = nullptr;
    const dmm::History *history = nullptr;
    const dmm::RelativeReading *relative = nullptr;
    unsigned display = 0;
    bool showStatistics = true;
    unsigned face = 0, palette = 0; // MeterStyles: the reading's typeface and the DMM colours.
    const MeterAppearance *appearance = nullptr; // Per-element overrides (null: none).
    // The hold mode's word for the state pill ("DELAY 5", "WAIT", "A-HOLD": HoldDetector::indicator);
    // null or empty: none (Manual, idle, or the manual/DMM HOLD, which reads "HOLD").
    const char *hold = nullptr;
    bool showBar = true;
    bool recording = false; // CSV logging: " REC" on the state pill.
    dmm::Function selecting = dmm::Function::Unknown; // Being selected (boot, restart): its sign over an empty reading.
};
} // namespace openosc::ui
