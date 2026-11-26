#pragma once
#include "openosc/instrument/mode.hpp"
#include "openosc/measurement/dmm/activity.hpp"
#include "openosc/platform/idle_policy.hpp"
#include <cstdint>

namespace openosc {
// GitHub #169, Settings > Power > Wake on measurement: whether the instrument on show had an event of
// its own this pass, which then counts as activity for the backlight and the idle power-off
// (ApplicationController::servicePower), as a key does but without taking one.
// - DMM (Wake DMM or Both, the multimeter on show): a reading that moved (dmm::ActivityDetector).
// - Scope (Scope or Both, the oscilloscope on show): a new record of a real trigger, a step of
//   DataAccess::scopeTriggerEvents since the last pass. The serial is followed in every pass, so
//   records counted while another instrument, Off or a busy instrument stood never count later.
// `instrument` false (a shutdown, AUTO, a calibration, a Bode sweep): nothing counts, and the DMM's
// baseline goes, as it does for another instrument or the option off.
class MeasurementWake {
public:
    struct Pass {
        IdleSettings::Wake wake = IdleSettings::Wake::Off;
        MeasurementMode mode = MeasurementMode::Oscilloscope;
        bool instrument = false;
        const dmm::Result *reading = nullptr; // The DMM's latest packet (null: none).
        bool usable = false;                  // Current, confirmed, not held (the controller's verdict).
        uint32_t triggers = 0;                // DataAccess::scopeTriggerEvents.
    };
    bool poll(const Pass &pass) {
        IdleSettings settings;
        settings.wake = pass.wake;
        bool activity = false;
        if (pass.instrument && settings.meterWakes() && pass.mode == MeasurementMode::Multimeter && pass.reading)
            activity = meter_.observe(*pass.reading, pass.usable);
        else meter_.reset();
        if (pass.instrument && settings.scopeWakes() && pass.mode == MeasurementMode::Oscilloscope && pass.triggers != triggers_)
            activity = true;
        triggers_ = pass.triggers;
        return activity;
    }
    const dmm::ActivityDetector &meter() const { return meter_; } // Tests.

private:
    dmm::ActivityDetector meter_;
    uint32_t triggers_ = 0;
};
} // namespace openosc
