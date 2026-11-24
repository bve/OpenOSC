#include "openosc/application/controller.hpp"
#include "openosc/application/generator_startup.hpp"
#include "openosc/board/current.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/settings/instrument_state.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/ui/screens/screen_layout.hpp"
#include "openosc/usb/instrument_usb.hpp"
#include <cstring>

namespace openosc {
namespace {
// A level set on purpose is the new anchor (TriggerFollow::forget). Issue #127: the code nearest its
// channel's zero shows 0 V (TriggerLevel::shownVolts), so set there it is anchored at 0 V: a later move
// of the zero or a V/div keeps it on the zero's nearest code. Its own frame (the ZT706S's stack).
[[gnu::noinline]] void anchorSetLevel(const DataAccess &data, scope::TriggerFollow &follow, const scope::TriggerFollow::Place &place) {
    const auto channel = place.channel < 2 ? data.scopeChannelFor(data.scopeRequest(), place.channel) : scope::ChannelMetadata{};
    if (channel.calibrated && place.level == std::lround(channel.zeroCode)) follow.anchor(place, channel.zeroCode - place.level, false);
    else follow.forget();
}
} // namespace
bool ApplicationController::meterTemperatureSettings(dmm::TemperatureSettings settings, uint32_t now) {
    if (!settings.valid()) return false;
    if (settings == prefs_.meter.temperature) return true;
    prefs_.meter.temperature = settings;
    data_.meter().configureTemperature(settings, data_.nowUs());
    changed(now);
    dataRefresh_ = true;
    return true;
}

// The saved settings onto the instruments (boot, Reload settings JSON). `prefs_` keeps what was saved
// wherever a part could not be applied (the screen shows what runs): a failed start never overwrites
// the user's choice in the next save with the fallback or a default.
PreferencesResult ApplicationController::applyDataPreferences() {
    PreferencesResult result;
    const auto previousMeasurement = data_.measurementMode();
    result.mode = data_.select(state_.mode);
    if (!result.mode) state_.mode = data_.measurementMode();
    Board::configurePe10(state_.mode == MeasurementMode::Generator);
    if (previousMeasurement != data_.measurementMode() && usb_) usb_->profileChanged(HAL_GetTick());
    data_.selectInterleave(scope::InterleaveMode(prefs_.scope.interleave));
    auto request = InstrumentState::scope(prefs_);
    if (!request.frontend.enabled[unsigned(request.trigger.channel)])
        request.trigger.channel = scope::Channel(unsigned(request.trigger.channel)^1);
    result.scope = data_.configureScope(request);
    result.generator = data_.generator().configure(InstrumentState::generator(prefs_));
    if (result.generator) InstrumentState::capture(data_.generator().settings(),prefs_);
    result.meterRequested = data_.measurementMode() == MeasurementMode::Multimeter;
    data_.meter().configureTemperature(prefs_.meter.temperature, data_.nowUs());
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
bool ApplicationController::selectInterleave(scope::InterleaveMode mode, uint32_t now) {
    if (!data_.selectInterleave(mode)) { view_.notify(Tooltip::Unavailable, "Cannot change interleave now", now); return false; }
    if (prefs_.scope.interleave != unsigned(mode)) {
        prefs_.scope.interleave = unsigned(mode);
        changed(now);
        view_.notify(mode == scope::InterleaveMode::On ? Tooltip::Advisory : Tooltip::Done, // On takes CH2 away.
                     mode == scope::InterleaveMode::On ? "Interleave on: CH1 alone"
                     : mode == scope::InterleaveMode::Auto ? "Interleave auto" : "Interleave off", now);
    }
    return true;
}
// The MODE key and SYST:MODE: one path. A different measurement changes the USB profile (the device
// reconnects); the DMM restores its saved function. Also in Remote and headless (SCPI): no UI then.
ApplicationController::ModeChange ApplicationController::selectMode(MeasurementMode next, uint32_t now) {
    if (next == state_.mode) return ModeChange::Selected;
    if (!cancelAuto(now)) return ModeChange::Busy;
    if (panel_ == Panel::Bode) finishBode(now, Tooltip::Done, nullptr); // Any path (SCPI too): the instrument restored first.
    if (bodeOwns()) return ModeChange::Busy; // Its restore refused (busy) or unconfirmed: tried again, then the mode.
    const auto previousMeasurement = data_.measurementMode();
    if (!data_.select(next)) return ModeChange::Refused; // The menu and its edit stay as they are.
    dropEdit(now);
    state_.mode = next;
    Board::configurePe10(state_.mode == MeasurementMode::Generator);
    ++counterEpoch_; // The counter overlay averages a Scope visit only.
    state_.settingsOpen = false;
    panel_ = Panel::None;
    if (previousMeasurement != data_.measurementMode() && usb_) usb_->profileChanged(now);
    if (state_.mode == MeasurementMode::Multimeter) {
        const bool requested = data_.selectDmmFunction(dmm::Function(prefs_.meterFunction));
        meterRestore_.arm(requested);
        if (!requested && uiReady_) view_.notify(Tooltip::Critical, "DMM function selection failed",now);
    }
    dataRefresh_=true;
    prefs_.mode = uint32_t(state_.mode);
    changed(now);
    if (uiReady_) view_.measurement(state_);
    return ModeChange::Selected;
}
bool ApplicationController::applyScope(const ScopeRequest &request, uint32_t now, ScopeApply mode) {
    const auto previousTrigger = data_.scopeRequest().trigger;
    const bool applied = mode == ScopeApply::Resume ? data_.resumeScope(request)
        : mode == ScopeApply::StageIfHeld && data_.scopeHeld() ? data_.stageScope(request) : data_.configureScope(request);
    if (!applied) return false;
    const auto &active = data_.scopeRequest();
    if (uiReady_ && (active.trigger.channel != previousTrigger.channel || active.trigger.mode != previousTrigger.mode))
        view_.waveform().triggerLimit(0);
    triggerFollow_.observe(triggerPlace(active));
    const auto previous = prefs_;
    InstrumentState::capture(active, prefs_);
    state_.scope.timebaseNs = prefs_.timebaseNs;
    for (unsigned c = 0; c < 2; ++c) state_.scope.millivoltsPerDivision[c] = prefs_.millivolts[c];
    if (std::memcmp(&previous, &prefs_, sizeof prefs_)) changed(now);
    dataRefresh_ = true;
    return true;
}
scope::TriggerFollow::Place ApplicationController::triggerPlace(const ScopeRequest &r) {
    const unsigned c = unsigned(r.trigger.channel) & 1U;
    return {int16_t(r.trigger.level), int16_t(r.frontend.position[c]), uint8_t(c), uint8_t(r.frontend.range[c])};
}
// A change of the trigger channel's scale (V/div) or analog zero moves its codes, so the trigger keeps
// its input voltage (TriggerFollow, GitHub #26). Applies to whoever changes them, the keys, the editors
// or a remote host; an explicit new level (TRIG:LEV, the trigger keys, 50 %) is applied as given, the
// other channel changes nothing, and a refused request leaves no trace (the anchor before it stays).
bool ApplicationController::applyFollowing(ScopeRequest request, uint32_t now, ScopeApply mode, LevelIntent intent,
                                           scope::TriggerLevel::Bounds bounds) {
    const auto &current=data_.scopeRequest();
    const unsigned c=unsigned(request.trigger.channel);
    const auto place = triggerPlace(current);
    const bool alreadyLimited = triggerFollow_.limited(place);
    scope::TriggerFollow::Proposal proposal;
    bool following=false;
    if (intent==LevelIntent::Follow && c<2 && request.trigger.channel==current.trigger.channel && request.trigger.level==current.trigger.level) {
        const auto before=data_.scopeChannelFor(current,c), after=data_.scopeChannelFor(request,c);
        if ((following=before.calibrated && after.calibrated && before.voltsPerCode>0 && after.voltsPerCode>0 &&
                       (before.voltsPerCode!=after.voltsPerCode || before.zeroCode!=after.zeroCode))) {
            proposal=triggerFollow_.propose(place,request.frontend.position[c],request.frontend.range[c],
                                            {before.zeroCode,before.voltsPerCode},{after.zeroCode,after.voltsPerCode},bounds);
            request.trigger.level=proposal.level;
        }
    }
    if (!applyScope(request,now,mode)) return false;
    if (intent==LevelIntent::Set) anchorSetLevel(data_,triggerFollow_,triggerPlace(data_.scopeRequest()));
    if (following) {
        triggerFollow_.commit(proposal);
        if (proposal.limited && !alreadyLimited) pulseTriggerLimit(now);
    }
    return true;
}
// Editors change the next acquisition even while stopped: HOLD keeps its record, and
// a finished Single shot, which is no user stop, arms the edited configuration.
bool ApplicationController::applyEdited(ScopeRequest request, uint32_t now, LevelIntent intent) {
    plot::XyMode::constrain(view_.waveform().options(),request);
    return applyFollowing(request,now,data_.scopeSingleDone() ? ScopeApply::Resume : ScopeApply::StageIfHeld,intent);
}
bool ApplicationController::applyFound(const ScopeRequest &request, uint32_t now) {
    if (!applyScope(request,now)) return false;
    triggerFollow_.forget();
    return true;
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
scope::TriggerLevel::Bounds ApplicationController::triggerBounds(const ScopeRequest &request) {
    const auto &waveform = view_.waveform();
    const auto &options = waveform.options();
    const unsigned source = unsigned(request.trigger.channel);
    if (source >= 2 || request.rolling() || (options.mode != plot::Mode::Time && options.mode != plot::Mode::SpectrumOverlay))
        return {};
    const auto &model = waveform.model();
    const auto &m = model.metadata();
    const auto target = data_.scopeChannelFor(request, source);
    const auto &shown = data_.scopeHeld() && model.hasFrame() && m.channels[source].enabled ? m.channels[source] : target;
    const int height = model.hasFrame() ? model.viewport().height : ui::ScreenLayout::PlotHeight;
    plot::Axis axis;
    if (height < 2 || !axis.configure(shown, options.channels[source], unsigned(height))) return {};
    const auto toRequested = [&](int y) { return scope::TriggerLevel::inFrame(axis.code(y), shown, target); };
    return scope::TriggerLevel::Bounds::between(toRequested(0), toRequested(height - 1));
}
int ApplicationController::triggerLimit(const ScopeRequest &request) {
    const auto &options = view_.waveform().options();
    const unsigned source = unsigned(request.trigger.channel);
    if (source >= 2 || request.rolling() || (options.mode != plot::Mode::Time && options.mode != plot::Mode::SpectrumOverlay)) return 0;
    const auto bounds = triggerBounds(request);
    const int level = request.trigger.level, end = level >= bounds.high ? 1 : level <= bounds.low ? -1 : 0;
    return options.channels[source].invert ? -end : end;
}
bool ApplicationController::markTriggerEditor() {
    auto request = data_.scopeRequest();
    request.trigger = triggerEditor_.settings();
    return triggerEditor_.levelBounds(triggerBounds(request));
}
bool ApplicationController::triggerMidpoint(uint32_t now, bool pretrigger) {
    auto request = data_.scopeRequest();
    if (pretrigger) request.trigger.position = 500; // Applied with the level: one restart.
    scope::FrameLease frame;
    double midpoint;
    if (!data_.acquireScopeWait(frame) || !ui::TriggerEditor::midpoint(frame.view(), request.trigger.channel, midpoint)) {
        frame.release(); // Applying restarts capture, which needs every frame lease back.
        if (!pretrigger || request.same(data_.scopeRequest())) {
            view_.notify(Tooltip::Unavailable, "50% needs a completed channel capture",now);
            return false;
        }
        if (!applyScope(request, now, ScopeApply::StageIfHeld)) {
            view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
            return false;
        }
        view_.notify(Tooltip::Advisory, "Pretrigger 50%; level needs a capture",now);
        return false;
    }
    const unsigned source = unsigned(request.trigger.channel);
    const auto &record = frame.view().metadata().channels[source];
    const auto target = data_.scopeChannelFor(request, source);
    const double exact = scope::TriggerLevel::inFrame(midpoint, record, target);
    frame.release(); // Applying restarts capture, which needs every frame lease back.
    request.trigger.level = triggerBounds(request).nearest(exact);
    const bool applied = pretrigger ? applyScope(request, now, ScopeApply::StageIfHeld)
                                    : applyEdited(request, now, LevelIntent::Set);
    if (!applied) { view_.notify(Tooltip::Unavailable, "Finish data transfer first", now); return false; }
    const auto place = triggerPlace(data_.scopeRequest());
    const double remainder = exact - place.level;
    const bool limited = std::abs(remainder) > .5;
    triggerFollow_.anchor(place, remainder, limited);
    if (limited) pulseTriggerLimit(now);
    if (pretrigger) view_.notify(Tooltip::Done, "Trigger level and pretrigger 50%", now);
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
    case Panel::Trigger: triggerKeys(keys, now, fine); break;
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
    case Panel::Bode: bodeKeys(KeyEvents{}, keys, now); break; // onKeys hands it on first (MODE included).
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
