// Scope calibration > Interleave offset (ZT703S): the controller's side of the measurement
// (DataAccess::beginInterleaveOffset): its start after the confirmation and its result.
#include "openosc/application/controller.hpp"
#include "openosc/ui/editors/confirmation.hpp"
#include <cstdio>
#include <cstdlib>

namespace openosc {
// MENU on the row: the confirmation (where the offset can be measured now), or the running measurement
// cancelled.
void ApplicationController::interleaveOffsetAction(uint32_t now) {
    if (data_.interleaveMeasurement().active()) { data_.cancelInterleaveOffset(); showSettings(); return; }
    if (!prefs_.scope.interleave) { view_.notify("Turn Interleave CH1 on first (Performance)", now); return; }
    if (data_.interleaveBlockers()) { view_.notify("Needs CH1 alone at 10/25 ns, no Roll/GEN", now); return; }
    confirmOperation(ui::Confirmation("GROUND CH1?","Measure interleave offset","Steady input; this clock only"),
                     &ApplicationController::beginInterleaveOffset);
}
// After its confirmation: back to Settings on the row (the handler forgotten), where the row shows
// the measurement, MENU on it cancels and the result is told; a refusal returns there too.
void ApplicationController::beginInterleaveOffset(uint32_t now) {
    leaveConfirmation(now);
    switch (data_.beginInterleaveOffset(now)) {
    case DataStatus::Ok: interleaveWatched_ = true; view_.notify("Measuring interleave offset...", now); break;
    case DataStatus::InvalidArgument: view_.notify("Needs Interleave active and trigger Auto", now); break;
    case DataStatus::Unsupported: view_.notify("No interleave offset on this model", now); break;
    default: view_.notify("Finish HOLD, calibration or recording first", now); break;
    }
    if (state_.settingsOpen) showSettings();
}
// Its result once, as it ends (each outcome says what to do; the profile in force changed only when saved).
void ApplicationController::serviceInterleaveOffsetUi(uint32_t now) {
    if (!interleaveWatched_) return;
    const auto &m = data_.interleaveMeasurement();
    if (m.active()) return;
    interleaveWatched_ = false;
    using Result = calibration::InterleaveOffsetMeasurement::Result;
    const auto tenths = [](double value) { return int(value * 10 + (value < 0 ? -0.5 : 0.5)); };
    char text[72];
    const int offset = tenths(m.offset()), spread = tenths(m.spread());
    switch (m.result()) {
    case Result::Measured:
        std::snprintf(text, sizeof text, "Interleave offset %c%d.%d saved (%lu MHz)", offset < 0 ? '-' : '+', std::abs(offset) / 10,
                      std::abs(offset) % 10, (unsigned long)(m.coreHz() / 1000000UL));
        break;
    case Result::NotQuiet: std::snprintf(text, sizeof text, "Input not quiet (%d.%d codes): ground CH1", spread / 10, spread % 10); break;
    case Result::Clipped: std::snprintf(text, sizeof text, "Input at the ADC limit: ground CH1, not saved"); break;
    case Result::OutOfRange: std::snprintf(text, sizeof text, "Interleave offset out of range: not saved"); break;
    case Result::Timeout: std::snprintf(text, sizeof text, "No records: interleave offset not saved"); break;
    case Result::SaveFailed: std::snprintf(text, sizeof text, "Cannot save interleave offset"); break;
    case Result::Cancelled: std::snprintf(text, sizeof text, "Interleave offset cancelled"); break;
    default: std::snprintf(text, sizeof text, "Interleave offset interrupted: not saved"); break;
    }
    view_.notify(text, now);
    if (state_.settingsOpen && !settingsMenu_.information()) showSettings();
}
} // namespace openosc
