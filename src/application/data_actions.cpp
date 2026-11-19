#include "openosc/application/controller.hpp"
#include "openosc/application/generator_startup.hpp"
#include "openosc/board/current.hpp"
#include "openosc/settings/instrument_state.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/usb/instrument_usb.hpp"
#include <cstring>

namespace openosc {
// The saved settings onto the instruments (boot, Reload settings JSON). `prefs_` keeps what was saved
// wherever a part could not be applied (the screen shows what runs): a failed start never overwrites
// the user's choice in the next save with the fallback or a default.
PreferencesResult ApplicationController::applyDataPreferences() {
    PreferencesResult result;
    const auto previousMeasurement = data_.measurementMode();
    result.mode = data_.select(state_.mode);
    if (!result.mode) state_.mode = data_.measurementMode();
    if (previousMeasurement != data_.measurementMode() && usb_) usb_->profileChanged(HAL_GetTick());
    // The ZT703S interleave choice before the request, so the first capture already follows it; any
    // other board takes only off (a file's on is kept, not applied).
    data_.selectInterleave(prefs_.scope.interleave && board::Current::Features.selectableInterleave);
    auto request = InstrumentState::scope(prefs_);
    if (!request.frontend.enabled[unsigned(request.trigger.channel)])
        request.trigger.channel = scope::Channel(unsigned(request.trigger.channel)^1);
    result.scope = data_.configureScope(request);
    result.generator = data_.generator().configure(InstrumentState::generator(prefs_));
    if (result.generator) InstrumentState::capture(data_.generator().settings(),prefs_);
    result.meterRequested = data_.measurementMode() == MeasurementMode::Multimeter;
    if (result.meterRequested) {
        result.meter = data_.selectDmmFunction(dmm::Function(prefs_.meterFunction));
        meterRestore_.arm(result.meter);
    }
    if (result.scope) InstrumentState::capture(data_.scopeRequest(), prefs_);
    // What the capture runs (the saved values when they were applied).
    state_.scope.timebaseNs = data_.scopeRequest().timebaseNs;
    for (unsigned c=0; c<2; ++c)
        state_.scope.millivoltsPerDivision[c] = scope::AnalogFrontend::millivoltsForRange(data_.scopeRequest().frontend.range[c]);
    dataRefresh_ = true;
    return result;
}
// The one path of the choice (menu, SCPI): the capture replanned at once, the setting saved, a notice.
bool ApplicationController::selectInterleave(bool on, uint32_t now) {
    if (!data_.selectInterleave(on)) { view_.notify("Finish calibration or AutoSetup first", now); return false; }
    if (prefs_.scope.interleave != unsigned(on)) {
        prefs_.scope.interleave = on;
        changed(now);
        view_.notify(on ? "Interleave on: CH1 alone, 10/25 ns" : "Interleave off", now);
    }
    return true;
}
// The MODE key and SYST:MODE: one path. A different measurement changes the USB profile (the device
// reconnects); the DMM restores its saved function. Also in Remote and headless (SCPI): no UI then.
ApplicationController::ModeChange ApplicationController::selectMode(MeasurementMode next, uint32_t now) {
    if (next == state_.mode) return ModeChange::Selected;
    if (!cancelAuto(now)) return ModeChange::Busy;
    const auto previousMeasurement = data_.measurementMode();
    if (!data_.select(next)) return ModeChange::Refused; // The menu and its edit stay as they are.
    dropEdit(now);
    state_.mode = next;
    state_.settingsOpen = false;
    panel_ = Panel::None;
    if (previousMeasurement != data_.measurementMode() && usb_) usb_->profileChanged(now);
    if (state_.mode == MeasurementMode::Multimeter) {
        const bool requested = data_.selectDmmFunction(dmm::Function(prefs_.meterFunction));
        meterRestore_.arm(requested);
        if (!requested && uiReady_) view_.notify("DMM function selection failed",now);
    }
    dataRefresh_=true;
    prefs_.mode = uint32_t(state_.mode);
    changed(now);
    if (uiReady_) view_.measurement(state_);
    return ModeChange::Selected;
}
bool ApplicationController::applyScope(const ScopeRequest &request, uint32_t now, ScopeApply mode) {
    const bool applied = mode == ScopeApply::Resume ? data_.resumeScope(request)
        : mode == ScopeApply::StageIfHeld && data_.scopeHeld() ? data_.stageScope(request) : data_.configureScope(request);
    if (!applied) return false;
    const auto &active = data_.scopeRequest();
    const unsigned source = unsigned(active.trigger.channel);
    triggerFollow_.observe(active.trigger.level, source, active.frontend.position[source]);
    const auto previous = prefs_;
    InstrumentState::capture(active, prefs_);
    state_.scope.timebaseNs = prefs_.timebaseNs;
    for (unsigned c = 0; c < 2; ++c) state_.scope.millivoltsPerDivision[c] = prefs_.millivolts[c];
    if (std::memcmp(&previous, &prefs_, sizeof prefs_)) changed(now);
    dataRefresh_ = true;
    return true;
}
// A move of the analog zero is a move of the whole trace, the trigger on it included.
// Applies to whoever moves it, the keys or a remote host; a refused request leaves no trace.
bool ApplicationController::applyFollowing(ScopeRequest request, uint32_t now, ScopeApply mode) {
    const auto &current=data_.scopeRequest();
    const unsigned c=unsigned(request.trigger.channel);
    scope::TriggerFollow::Proposal proposal;
    bool following=false;
    if (request.trigger.channel==current.trigger.channel && request.trigger.level==current.trigger.level &&
        request.frontend.position[c]!=current.frontend.position[c]) {
        const auto before=data_.scopeChannelFor(current,c), after=data_.scopeChannelFor(request,c);
        if ((following=before.calibrated && after.calibrated)) {
            proposal=triggerFollow_.propose(current.trigger.level,c,current.frontend.position[c],request.frontend.position[c],before.zeroCode,after.zeroCode);
            request.trigger.level=proposal.level;
        }
    }
    if (!applyScope(request,now,mode)) return false;
    if (following) triggerFollow_.commit(proposal);
    return true;
}
// Editors change the next acquisition even while stopped: HOLD keeps its record, and
// a finished Single shot, which is no user stop, arms the edited configuration.
bool ApplicationController::applyEdited(ScopeRequest request, uint32_t now) {
    plot::XyMode::constrain(view_.waveform().options(),request);
    return applyFollowing(request,now,data_.scopeSingleDone() ? ScopeApply::Resume : ScopeApply::StageIfHeld);
}
// What a held record contains decides what can be examined; a running view follows the next capture.
bool ApplicationController::channelShown(unsigned channel) {
    const auto &model=view_.waveform().model();
    return data_.scopeHeld() && model.hasFrame() ? model.metadata().channels[channel].enabled : data_.scopeRequest().frontend.enabled[channel];
}
// One rule for the main scale/position selection, whatever changed the channels (panel, SCPI,
// settings at start-up, a new frame after RUN): it stays on a channel that is shown. The cards
// and context follow at once, without waiting for a new sequence (a held frame keeps its own).
bool ApplicationController::followShownChannel() {
    if (channelShown(selectedChannel_) || !channelShown(selectedChannel_ ^ 1)) return false;
    selectedChannel_ ^= 1;
    dataRefresh_ = true;
    return true;
}
bool ApplicationController::triggerMidpoint(uint32_t now) {
    triggerEditor_.begin(data_.scopeRequest().trigger);
    scope::FrameLease frame;
    if (!data_.acquireScopeWait(frame) || !triggerEditor_.midpoint(frame.view())) {
        view_.notify("50% needs a completed channel capture",now);
        return false;
    }
    frame.release(); // Applying restarts capture, which needs every frame lease back.
    auto request=data_.scopeRequest(); request.trigger=triggerEditor_.settings();
    if (!applyEdited(request,now)) { view_.notify("Finish data transfer first",now); return false; }
    return true;
}
bool ApplicationController::restoreStartupOutput(const PreferencesResult &applied) {
    if (!GeneratorStartup::restores(prefs_, applied.generator)) return false;
    return requestGeneratorOutput(true, 0); // Refused: stays off; the kept state stays as it was.
}
bool ApplicationController::requestGeneratorOutput(bool on, uint32_t now) {
    auto &output = data_.generator();
    if (on && data_.zeroBusy()) return false;
    startup_.requested(on);
    if (on && !output.enable()) { startup_.refused(); return false; }
    if (!on) output.disable();
    followGeneratorOutput(now);
    dataRefresh_ = true;
    return on ? output.enabled() : !output.enabled();
}
void ApplicationController::followGeneratorOutput(uint32_t now) {
    if (startup_.follow(prefs_, GeneratorStartup::confirmed(data_.generator())) && prefs_.generatorStartup) changed(now);
}
bool ApplicationController::applyGenerator(const generator::Settings &settings, uint32_t now) {
    if (data_.zeroBusy()) return false;
    if (!data_.generator().configure(settings)) return false;
    const auto &actual = data_.generator().settings();
    InstrumentState::capture(actual,prefs_);
    changed(now); dataRefresh_ = true; return true;
}

void ApplicationController::panelKeys(uint32_t keys, uint32_t now, bool fine) {
    switch (panel_) {
    case Panel::Display: plotKeys(keys, now, fine); break;
    case Panel::Trigger: triggerKeys(keys, now); break;
    case Panel::Frequency: frequencyKeys(keys, now); break;
    case Panel::Meter: meterPanelKeys(keys, now); break;
    case Panel::MeterAppearance: meterAppearanceKeys(keys, now); break;
    case Panel::MeterHold: meterHoldKeys(keys, now); break;
    case Panel::MeterLayout: meterLayoutKeys(keys, now); break;
    case Panel::ScopeLayout: scopeLayoutKeys(keys, now); break;
    case Panel::GeneratorLayout: generatorLayoutKeys(keys, now); break;
    case Panel::ScopeColors: scopeColorsKeys(keys, now); break;
    case Panel::Analyze: analyzeKeys(keys, now); break;
    case Panel::Decoder: decoderPanelKeys(keys, now); break;
    case Panel::Channel:
    case Panel::Acquisition: scopeEditorKeys(keys, now); break;
    case Panel::Measurements: measurementKeys(keys, now); break;
    case Panel::Generator: generatorEditorKeys(keys, now); break;
    default: dataKeys(keys, now, fine); break;
    }
}
void ApplicationController::dataKeys(uint32_t keys, uint32_t now, bool fine) {
    if (!keys) return;
    analyzeRow_ = ~0U; // No panel open (also after MODE, which drops it without closePanel).
    switch (state_.mode) {
    case MeasurementMode::Oscilloscope: if (decoderScreen()) decoderKeys(keys, now); else scopeKeys(keys, now, fine); break;
    case MeasurementMode::Multimeter: meterKeys(keys, now); break;
    case MeasurementMode::Generator: generatorKeys(keys, now); break;
    case MeasurementMode::Count: break;
    }
    dataRefresh_ = true;
}
} // namespace openosc
