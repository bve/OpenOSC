#include "openosc/application/controller.hpp"
#include <cmath>
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/usb/instrument_usb.hpp"

namespace openosc {
ui::SettingsProperties ApplicationController::settingsRows() const {
    ui::CalibrationStatus status;
    const auto &d = calibrationDraft_;
    const auto &request = data_.scopeRequest();
    status.probeNumerator = request.probeNumerator[d.channel]; status.probeDenominator = request.probeDenominator[d.channel];
    status.spanKnown = data_.gainSpan(d.channel, d.range, d.referenceMillivolts / 1000.0, status.span);
    status.gainInForce = data_.gainInForce(d.channel, d.range);
    for (unsigned c = 0; c < 2; ++c) status.factoryGainHundredths[c] = uint16_t(std::lround(data_.factoryScaleGain(c) * 100));
    status.interleaveInForce = data_.interleaveOffset();
    status.interleaveSaved = data_.savedInterleave().offset; status.interleaveSavedEnabled = data_.savedInterleave().enabled;
    ui::SettingsProperties rows(settingsMenu_, prefs_, ClockControl::activeMode(), calibrationDraft_, status);
    const auto *clocked = data_.interleaveOffsetAtClock();
    rows.interleave({data_.interleaveBlockers(), data_.interleaveCalibrated(), clocked ? *clocked : 0.f,
                     data_.interleaveMeasurement().active()});
    return rows;
}
void ApplicationController::closeSettings() {
    state_.settingsOpen = false;
    dataRefresh_ = true;
    view_.measurement(state_);
}
bool ApplicationController::dropEdit(uint32_t now) {
    if (!state_.settingsOpen || settingsMenu_.information()) { navigator_.reset(navigator_.row()); return false; }
    auto rows = settingsRows();
    if (!navigator_.cancel(rows)) return false;
    calibrationDraft_ = rows.calibration();
    applySettings(rows.preferences(), now);
    return true;
}
void ApplicationController::settingsKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto back = [&] {
        if (!settingsMenu_.back()) { closeSettings(); return false; }
        navigator_.reset(settingsMenu_.selected());
        return true;
    };
    if (settingsMenu_.information()) { // About, Storage: any of MENU, F4 or Left returns.
        if ((keys & (Keypad::F4 | Keypad::Menu | (navigator_.repeating(Keypad::Left) ? 0U : uint32_t(Keypad::Left)))) && back()) showSettings();
        return;
    }
    auto rows = settingsRows();
    const auto outcome = navigator_.keys(keys, rows);
    settingsMenu_.select(navigator_.row());
    switch (outcome) {
    case Outcome::Back:
        if (!back()) return;
        break;
    case Outcome::Open: {
        const auto action = settingsMenu_.enter(navigator_.row());
        navigator_.reset(settingsMenu_.selected());
        settingsAction(action, now);
        break;
    }
    case Outcome::Activate:
        if (settingsMenu_.current().action == ui::SettingsAction::Back) { if (!back()) return; }
        else if (settingsMenu_.current().action == ui::SettingsAction::GeneratorLayout) { openGeneratorLayout(true); return; } // A panel, not an action.
        else settingsAction(settingsMenu_.current().action, now);
        break;
    case Outcome::Changed: // A committed field; the interleave offset's edit puts it in force (not saved).
        calibrationDraft_ = rows.calibration();
        if (settingsMenu_.current().action == ui::SettingsAction::InterleaveOffset) interleaveAction(ui::SettingsAction::InterleaveOffset, now);
        else applySettings(rows.preferences(), now, settingsMenu_.current().action == ui::SettingsAction::RunProfile);
        break;
    case Outcome::Stepped: case Outcome::Restored: // A number in progress: brightness previews live, the rest is a draft.
        calibrationDraft_ = rows.calibration(); applySettings(rows.preferences(), now); break;
    default: break;
    }
    if (state_.settingsOpen) showSettings();
}
// A Settings choice (or a brightness step, or its F4 restore): what differs from the preferences in
// force is applied; run mode and record memory through the instrument, which may refuse them.
void ApplicationController::applySettings(const Preferences &chosen, uint32_t now, bool runModeChosen) {
    if (RunModeChoice::changes(chosen.scope.runMode, prefs_.scope.runMode, ClockControl::activeMode(), runModeChosen)) {
        const auto result = changeRunMode(chosen.scope.runMode, now);
        char text[64];
        if (result.outcome == RunModeSwitch::Outcome::Applied && result.active == OverclockedRunMode)
            std::snprintf(text, sizeof text, "%lu MHz applied (exceeds spec)",
                          static_cast<unsigned long>(RunModes[OverclockedRunMode].coreHz / 1000000));
        else if (result.active < RunModeCount)
            std::snprintf(text, sizeof text, "%s: %lu MHz", RunModeSwitch::describe(result.outcome),
                          static_cast<unsigned long>(RunModes[result.active].coreHz / 1000000));
        else std::snprintf(text, sizeof text, "%s", RunModeSwitch::describe(result.outcome));
        using Outcome = RunModeSwitch::Outcome;
        const auto kind = result.outcome == Outcome::Refused ? Tooltip::Unavailable :
                          !RemoteClock::settled(result) ? Tooltip::Critical :
                          result.outcome == Outcome::Applied && result.active == OverclockedRunMode ? Tooltip::Advisory : Tooltip::Done;
        view_.notify(kind, text, now);
    }
    if (chosen.scope.recordPoints != prefs_.scope.recordPoints) {
        auto request = data_.scopeRequest();
        request.recordPoints = chosen.scope.recordPoints;
        if (!applyEdited(request, now)) view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
    }
    if (chosen.usbController != prefs_.usbController) { // At once: off stops the controller, on attaches it.
        prefs_.usbController = chosen.usbController;
        if (usb_) usb_->enable(prefs_.usbController != 0, now);
        view_.notify(Tooltip::Done, prefs_.usbController ? "USB controller enabled" : "USB controller off", now);
    }
    if (chosen.scope.interleave != prefs_.scope.interleave) selectInterleave(scope::InterleaveMode(chosen.scope.interleave), now);
    if (chosen.showFps != prefs_.showFps) counterStale_ = true; // Redrawn on the next display pass.
    const bool idle = chosen.idle.backlightSeconds != prefs_.idle.backlightSeconds || chosen.idle.dimPercent != prefs_.idle.dimPercent ||
                      chosen.idle.powerOffMinutes != prefs_.idle.powerOffMinutes;
    prefs_.brightness = chosen.brightness; prefs_.showFps = chosen.showFps; prefs_.batteryDisplay = chosen.batteryDisplay;
    prefs_.batteryGainPermille = chosen.batteryGainPermille; prefs_.batteryOffsetMv = chosen.batteryOffsetMv;
    battery_.trim(prefs_.batteryGainPermille, prefs_.batteryOffsetMv); // Shown at once, the low verdict untouched.
    prefs_.idle = chosen.idle;
    prefs_.generatorStartup = chosen.generatorStartup; // The confirmed state is already followed.
    prefs_.panReverse = chosen.panReverse; prefs_.holdTime = chosen.holdTime;
    prefs_.clockIndicator = chosen.clockIndicator; // Shown or hidden on the next display pass.
    // Swap CH1/CH2: relabelled at once, a held record included; no capture, option or readout changes.
    prefs_.swapChannels = chosen.swapChannels; view_.channelView(plot::ChannelView{prefs_.swapChannels != 0});
    prefs_.tooltipsHidden = chosen.tooltipsHidden; view_.tooltips(prefs_.tooltipsHidden); // A notice of a kind hidden goes now.
    prefs_.scope.keys = chosen.scope.keys; // Read by the next AUTO, F2 or F4 stroke.
    if (idle) { idle_.configure(prefs_.idle, now); dimmed_ = false; }
    applyBrightness();
    changed(now);
}
} // namespace openosc
