#include "openosc/application/controller.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/usb/instrument_usb.hpp"

namespace openosc {
// The one place the backlight follows the settings: the working brightness, or while idle the
// chosen idle level (never brighter than it). Headless: no backlight to drive.
void ApplicationController::applyBrightness() {
    if (uiReady_) PowerControl::brightness(dimmed_ ? prefs_.idle.dimmedBrightness(prefs_.brightness) : prefs_.brightness);
}
// The DMM's packet is usable while current, confirmed (no selection under way) and not held (the
// manual HOLD, the meter's own, a hold mode's capture); a thermocouple reads millivolts, never a range.
// Off or another instrument on show: nothing else is read, only the trigger serial is followed.
bool ApplicationController::measurementActivity() {
    MeasurementWake::Pass pass;
    pass.wake = prefs_.idle.wake;
    pass.mode = state_.mode;
    pass.triggers = data_.scopeTriggerEvents();
    const bool meterWakes = prefs_.idle.meterWakes() && state_.mode == MeasurementMode::Multimeter;
    const bool scopeWakes = prefs_.idle.scopeWakes() && state_.mode == MeasurementMode::Oscilloscope;
    if (meterWakes || scopeWakes)
        pass.instrument = shutdown_ == Shutdown::None && !autoBusy() && !bodeOwns() && !data_.zeroBusy();
    if (meterWakes && pass.instrument) {
        const auto &meter = data_.meter();
        pass.reading = &meter.latest();
        pass.usable = meter.status(data_.nowUs(), 2000000) == DataStatus::Ok && !meter.awaitingFunction() &&
                      data_.dmmSelectionStatus() != DataStatus::Busy && !meter.held() && !meter.holding() && !meter.thermocouple();
    }
    return measurementWake_.poll(pass);
}
bool ApplicationController::servicePower(uint32_t keys,uint32_t now,bool measured) {
    battery_.poll(now);
    if (uiReady_) { view_.batteryDisplay(prefs_.batteryDisplay); view_.battery(battery_.state()); }
    // The idle level the key was pressed under, before its activity restores the working brightness.
    const bool wake=keys && prefs_.idle.wakeTakesKey(dimmed_,prefs_.brightness);
    if (keys) idle_.activity(now);
    if (shutdown_!=Shutdown::None) return wake;
    if (measured) idle_.measured(now); // The backlight and the idle power-off restart; no key is taken.
    const auto low=lowBattery_.poll(battery_.state(),now);
    if (low.warning && uiReady_) view_.notify(Tooltip::Critical, "Low battery: power off in 20 seconds",now);
    if (low.powerOff) {
        confirmAction_=ShutdownAction::PowerOff; requestShutdown(now); return wake;
    }
    // Work in progress keeps the instrument on: a configured USB host, Remote, GEN output, Serial DMM,
    // recordings, AUTO, calibrations, screenshots and image files. External power (PC9,
    // BatteryState::external) does not keep it on. PC9 is not proven VBUS: on a ZT703S it stayed HIGH on
    // battery after the cable was pulled (GitHub #23), and it then held the idle timer forever (#104).
    const bool inhibited=remote_ || (usb_ && usb_->connected()) || data_.generator().enabled() ||
        autoBusy() || data_.zeroBusy() || shots_.busy() || imageReader_.busy() || images_.clearing() || data_.scopeRecording() || data_.meterLogger().active() || data_.meterSession().active() ||
        (captures_ && captures_->clearing()) || data_.meterSerial().enabled();
    const auto state=idle_.poll(now,inhibited);
    if (state.dimmed!=dimmed_) {
        dimmed_=state.dimmed;
        applyBrightness();
    }
    if (state.warning && uiReady_) view_.notify(Tooltip::Critical, "Idle power off in 20 seconds",now);
    if (state.powerOff) { confirmAction_=ShutdownAction::PowerOff; requestShutdown(now); }
    return wake;
}
} // namespace openosc
