#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/scope_actions.hpp"
#include "openosc/acquisition/timebases.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/ui/scope/display_scale.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/ui/scope/view_zoom.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc {
namespace {
int axis(uint32_t keys, uint32_t negative, uint32_t positive) {
    return (keys & positive ? 1 : 0) - (keys & negative ? 1 : 0);
}
// Its own frame, after the apply has returned: the text is not on the stack during the capture restart.
[[gnu::noinline]] void notifyPretrigger(ApplicationView &view, unsigned position, bool held, uint32_t now) {
    char text[32];
    std::snprintf(text, sizeof text, "Pretrigger %u.%u%%%s", position / 10, position % 10, held ? " for RUN" : "");
    view.notify(Tooltip::Done, text, now);
}
// Left/Right: the next capture's time/div, one 1-2-5 step, in RUN and HOLD alike (issue #7).
void stepTimebase(ScopeRequest &request, uint32_t keys) {
    if (keys & Keypad::Right) request.timebaseNs = scope::Timebases::step(request.timebaseNs, 1);
    if (keys & Keypad::Left) request.timebaseNs = scope::Timebases::step(request.timebaseNs, -1);
}
}
void ApplicationController::setScopeFocus(plot::Focus focus) {
    focus_ = focus;
    view_.waveform().cursorFocus(focus == plot::Focus::Cursor ? cursors_.line() : plot::CursorLine::None);
    view_.scopeContext(selectedChannel_, focus_, prefs_.panReverse != 0);
}
// The active line has to be one that is drawn: follow every change of the cursor mode or view.
void ApplicationController::syncCursorFocus() {
    if (focus_ != plot::Focus::Cursor) return;
    const auto &options = view_.waveform().options();
    if (!plot::CursorControl::available(options)) { setScopeFocus(plot::Focus::Scale); return; }
    cursors_.sync(options.cursors);
    setScopeFocus(plot::Focus::Cursor);
}
void ApplicationController::openAnalyze(unsigned row) {
    openPanel(Panel::Analyze, ui::ScopeActions(view_.waveform().options()), row);
}
void ApplicationController::enterCursors(uint32_t now) {
    auto cursors = view_.waveform().options().cursors;
    if (!cursors.time && !cursors.voltage) cursors.time = cursors.voltage = true;
    cursors.channel = channelShown(selectedChannel_) ? selectedChannel_ : selectedChannel_ ^ 1;
    view_.waveform().cursorLines(cursors);
    cursors_.enter(cursors);
    setScopeFocus(plot::Focus::Cursor);
    queuePlotSave(now);
}
void ApplicationController::moveCursor(uint32_t keys, uint32_t now, bool fine) {
    const auto &view = view_.waveform().model().viewport();
    auto cursors = view_.waveform().options().cursors;
    if (!cursors_.move(cursors, axis(keys, Keypad::Left, Keypad::Right), axis(keys, Keypad::Up, Keypad::Down), fine, view.width, view.height)) return;
    view_.waveform().cursorLines(cursors);
    queuePlotSave(now);
}
void ApplicationController::resetPan(uint32_t now) {
    if (!view_.waveform().options().panPpb) return;
    const bool held = data_.scopeHeld();
    if (held) beginHeldView();
    auto options = view_.waveform().options(); options.panPpb = 0;
    view_.waveform().configure(options);
    if (!held) queuePlotSave(now);
}
bool ApplicationController::centerTrigger(uint32_t now) {
    auto options = view_.waveform().options();
    if (options.mode != plot::Mode::Time && options.mode != plot::Mode::SpectrumOverlay) {
        view_.notify(Tooltip::Unavailable, "Center trigger needs Y-T", now);
        return false;
    }
    auto request = data_.scopeRequest();
    const bool held = data_.scopeHeld();
    request.trigger.position = 500;
    if (!request.same(data_.scopeRequest()) && !applyScope(request, now, ScopeApply::StageIfHeld)) {
        view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
        return false;
    }
    bool plotChanged = options.panPpb || !options.lockTrigger;
    if (held) {
        beginHeldView();
        plotChanged |= heldView_.panPpb != 0;
        heldView_.panPpb = 0; // Use the centered start for the next RUN as well.
    }
    options.panPpb = 0;
    options.lockTrigger = true;
    options.centerHeldTrigger = held;
    view_.waveform().configure(options);
    if (plotChanged) queuePlotSave(now);
    dataRefresh_ = true;
    view_.notify(Tooltip::Done, "Trigger centered", now); // Also when already centered: the action is done (#65).
    return true;
}
void ApplicationController::beginHeldView() {
    if (heldViewActive_) return;
    heldView_ = view_.waveform().options();
    heldViewActive_ = true;
    heldViewRelease_ = data_.scopeHoldReleases();
}
int ApplicationController::triggerStep(int direction, bool fine) {
    constexpr unsigned DoubleEvery = 8, MaxRepeats = 3 * DoubleEvery;
    triggerRepeats_ = fine || direction != triggerSign_ ? 0 : std::min<unsigned>(MaxRepeats, triggerRepeats_ + 1);
    triggerSign_ = int8_t(direction);
    return direction * (1 << (triggerRepeats_ / DoubleEvery));
}
// Issue #7, as on the factory firmware: the time/div is shared by HOLD and RUN. A held record is
// shown at the next capture's time/div, whoever chose it (Left/Right, the Timebase menu, a host),
// and RUN keeps it; the record's metadata, lease and capture are untouched. FFT keeps its own axis.
// Issue #93: so is each channel's V/div (its range and probe, F1 Up/Down, the Channel menu, a host),
// for a calibrated record (an uncalibrated one has no volts to rescale: the change stays pending).
void ApplicationController::followNextCapture() {
    const auto &model = view_.waveform().model();
    const auto &m = model.metadata();
    const auto &request = data_.scopeRequest();
    const auto &next = data_.scopeMetadata();
    const auto &current = view_.waveform().options();
    const bool held = data_.scopeHeld() && model.hasFrame() && current.mode != plot::Mode::Spectrum;
    if (!held && !current.heldTimebaseNs && !current.channels[0].heldMillivoltsAtTip && !current.channels[1].heldMillivoltsAtTip)
        return;
    auto options = current;
    options.heldTimebaseNs = held && m.timebaseNs && request.timebaseNs != m.timebaseNs ? request.timebaseNs : 0;
    bool scales = false;
    for (unsigned c = 0; c < 2; ++c) {
        const auto &own = m.channels[c];
        const auto &target = next.channels[c];
        const bool differs = held && own.enabled && own.calibrated &&
            (target.millivoltsPerDivision != own.millivoltsPerDivision || target.probeNumerator != own.probeNumerator ||
             target.probeDenominator != own.probeDenominator);
        auto &o = options.channels[c];
        o.heldMillivoltsAtTip = differs ? float(plot::DisplayScale::nominalMillivolts(target)) : 0;
        scales |= o.heldMillivoltsAtTip != current.channels[c].heldMillivoltsAtTip;
    }
    const bool time = options.heldTimebaseNs != current.heldTimebaseNs;
    if (!time && !scales) return;
    if (held) beginHeldView();
    if (held && time) { // The chosen time/div replaces the zoom, here and after RUN.
        options.zoom = options.zoomOut = 1;
        heldView_.zoom = heldView_.zoomOut = 1;
    }
    view_.waveform().configure(options);
    dataRefresh_ = true;
}
// A press must not change the view unseen (issue #89): with no record on show, or one whose memory a
// capture waiting for its trigger (WAIT) took, nothing redraws it. A running capture that waits for
// no edge brings its next record in the new view, also when it reuses the shown record's memory
// meanwhile, as a single slot does on the ZT703S (issue #106). Roll redraws itself; HOLD keeps its record.
bool ApplicationController::viewFollows() const {
    const auto &model = view_.waveform().model();
    if (!model.hasFrame()) return false;
    return model.metadata().rolling || data_.scopeRecordStored() || (!data_.scopeHeld() && !data_.scopeWaiting());
}
void ApplicationController::moveTrace(uint32_t keys, uint32_t now, bool fine) {
    const bool held = data_.scopeHeld();
    if (held) beginHeldView();
    auto request = data_.scopeRequest();
    auto options = view_.waveform().options();
    // "Move waveform": Right shows earlier samples (X-Y: the axis shift keeps its sign).
    int horizontal = axis(keys, Keypad::Left, Keypad::Right) * (prefs_.panReverse && options.mode != plot::Mode::XY ? -1 : 1);
    // A held key (S09): x1, x2, x4, x8, doubling every PanDoubling repeats; a new press or the other
    // direction starts again at x1.
    const int sign = (horizontal > 0) - (horizontal < 0);
    panRepeats_ = fine || !sign || sign != panSign_ ? 0 : panRepeats_ + 1;
    panSign_ = sign;
    if (options.mode != plot::Mode::XY) horizontal *= 1 << std::min(3U, panRepeats_ / PanDoubling);
    const auto &model = view_.waveform().model();
    if (horizontal && options.mode != plot::Mode::XY && !viewFollows()) {
        if (fine) view_.notify(Tooltip::Unavailable, "No record to move", now); // Once per press: a held key repeats silently.
        horizontal = 0;
    }
    const auto change = focus_ == plot::Focus::Zoom // Pan only: no channel moves.
        ? plot::TraceMove::Change{false, plot::TraceMove::pan(options, horizontal, &model, false)}
        : plot::TraceMove::apply(request, options, selectedChannel_, held, horizontal, axis(keys, Keypad::Up, Keypad::Down), fine,
                                 &model);
    if (change.request && !applyEdited(request, now)) view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
    if (change.view) {
        view_.waveform().configure(options);
        if (!held) queuePlotSave(now);
    }
}
// Issue #123: the Trigger card's Left/Right set the pretrigger, the share of the next records before their
// trigger, 0-100 %: a press 1 %, a held key 5 %. Right adds history before the trigger. Held (HOLD or a
// finished Single), it is staged for the next capture and the held record and its view stay (as Center
// trigger); Roll has no trigger to place.
void ApplicationController::movePretrigger(uint32_t arrows, uint32_t now, bool fine) {
    const int direction = axis(arrows, Keypad::Left, Keypad::Right);
    if (!direction) return;
    auto request = data_.scopeRequest();
    if (request.rolling()) {
        if (fine) view_.notify(Tooltip::Unavailable, "Pretrigger: not in Roll", now);
        return;
    }
    const int position = std::clamp(int(request.trigger.position) + direction * (fine ? 10 : 50), 0, 1000);
    if (position == request.trigger.position) {
        if (fine) view_.notify(Tooltip::Unavailable, position ? "Pretrigger at 100%" : "Pretrigger at 0%", now);
        return;
    }
    request.trigger.position = uint16_t(position);
    if (!applyScope(request, now, ScopeApply::StageIfHeld)) {
        view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
        return;
    }
    notifyPretrigger(view_, unsigned(position), data_.scopeHeld(), now);
}
bool ApplicationController::holdKeys(uint32_t keys, uint32_t now) {
    if (!(keys & Keypad::HoldSave)) return false;
    if (!data_.scopeHeld()) data_.holdScope(true);
    else if (!data_.runScope() && uiReady_) view_.notify(Tooltip::Unavailable, "Cannot run: the record is in use", now);
    return true;
}
void ApplicationController::scopeKeys(uint32_t keys, uint32_t now, bool fine) {
    keys &= ~uint32_t(Keypad::AutoRange); // AUTO has strokes of its own (scopeAutoKeys).
    if (!keys) return;
    followShownChannel(); // Before any key acts on the selection.
    constexpr uint32_t Arrows = Keypad::Up | Keypad::Down | Keypad::Left | Keypad::Right;
    if (autoBusy()) {
        // F4 cancels Auto Setup, HOLD cancels it back to a held record; a Scale arrow cancels it and its
        // V/div or time/div step follows once the starting acquisition is back (finishAutoStep).
        if (keys & (Keypad::F4 | Keypad::HoldSave)) cancelAuto(now, keys & Keypad::HoldSave);
        else if (focus_ == plot::Focus::Scale && (keys & Arrows)) {
            autoStep_ = uint16_t(keys & Arrows); // The latest request; applied once (cancelAuto or serviceAuto).
            cancelAuto(now);
        }
        return;
    }
    syncCursorFocus();
    syncZoomFocus();
    const auto openTrigger=[&] {
        triggerEditor_.begin(data_.scopeRequest().trigger);
        triggerEditor_.sources(data_.scopeRequest().frontend.enabled[0], data_.scopeRequest().frontend.enabled[1]);
        triggerEditor_.calibration(data_.scopeMetadata());
        markTriggerEditor();
        openPanel(Panel::Trigger, triggerEditor_);
    };
    // F3 (issue #95): from another card only the Trigger card; on it each tap the next mode, Auto,
    // Normal, Single (held: staged for the next RUN). The source is in its menu.
    if (keys & Keypad::F3) {
        if (focus_ != plot::Focus::Trigger) setScopeFocus(plot::Focus::Trigger);
        else triggerModeStep(now);
        return;
    }
    if (keys & Keypad::Menu) { // Zoom & Pan: the menu of the card it came from.
        const auto card = focus_ == plot::Focus::Zoom ? zoomReturn_ : focus_;
        if (card == plot::Focus::Trigger) { openTrigger(); return; }
        if (card == plot::Focus::Cursor) { openAnalyze(ui::ScopeActions::Cursors); return; }
        channelEditor_.begin(data_.scopeRequest(), view_.waveform().options(), selectedChannel_);
        openPanel(Panel::Channel, channelEditor_);
        return;
    }
    if (keys & Keypad::F4) {
        const auto &cursors = view_.waveform().options().cursors;
        if (focus_ == plot::Focus::Zoom) leaveZoom(); // Back, the view as zoomed and panned.
        else if (focus_ == plot::Focus::Cursor) { // Next line, as on the factory firmware.
            cursors_.next(cursors);
            setScopeFocus(plot::Focus::Cursor);
        } else if (plot::CursorControl::available(view_.waveform().options())) enterCursors(now);
        else openAnalyze(0);
        return;
    }
    holdKeys(keys, now);
    if (keys & Keypad::Relative) view_.scopeFooterPage(); // REL has no other Scope use; its hold is power.
    // A second F1/F2 press changes the channel only to one that is shown (followShownChannel).
    if (keys & Keypad::F1) {
        if (focus_ == plot::Focus::Scale && channelShown(selectedChannel_ ^ 1)) selectedChannel_ ^= 1;
        focus_ = plot::Focus::Scale;
    }
    if (keys & Keypad::F2) {
        if (focus_ == plot::Focus::Position && channelShown(selectedChannel_ ^ 1)) selectedChannel_ ^= 1;
        focus_ = plot::Focus::Position;
    }
    setScopeFocus(focus_);
    const uint32_t arrows = keys & Arrows;
    if (!arrows) return;
    if (focus_ == plot::Focus::Zoom) { zoomKeys(arrows, now, fine); return; }
    if (focus_ == plot::Focus::Cursor) { moveCursor(arrows, now, fine); return; }
    if (focus_ == plot::Focus::Trigger && (arrows & (Keypad::Up | Keypad::Down))) {
        const int direction = axis(arrows, Keypad::Down, Keypad::Up);
        if (!direction) return;
        auto request = data_.scopeRequest();
        const auto bounds = triggerBounds(request);
        const auto before = request.trigger.level;
        const int step = triggerStep(direction, fine);
        const bool invert = view_.waveform().options().channels[unsigned(request.trigger.channel)].invert;
        const int delta = invert ? -step : step;
        request.trigger.level = bounds.clamp(bounds.clamp(before) + delta);
        if (request.trigger.level == before) {
            if (fine) { // A fresh press at the limit sets the current level as the anchor, without refreshing the plot.
                triggerFollow_.forget();
                pulseTriggerLimit(now);
            }
            return;
        }
        if (!applyEdited(request, now, LevelIntent::Set)) { view_.notify(Tooltip::Unavailable, "Finish data transfer first", now); return; }
        if (request.trigger.level == (delta > 0 ? bounds.high : bounds.low)) pulseTriggerLimit(now); // Each press against it.
        return;
    }
    if (focus_ == plot::Focus::Trigger) { movePretrigger(arrows, now, fine); return; } // F2 keeps the pan (issue #123).
    if (focus_ == plot::Focus::Position) { moveTrace(arrows, now, fine); return; }
    scaleKeys(arrows, now);
}
// F1: Up/Down the selected channel's V/div, Left/Right the time/div, one standard step each, in RUN
// and HOLD alike. A held record cannot be re-acquired: they stage the next capture (no relay
// switches, no new arm; RUN applies them) and the record is shown at them (followNextCapture,
// issues #7, #93). A held FFT's Left/Right zoom its bins instead.
void ApplicationController::scaleKeys(uint32_t keys, uint32_t now) {
    const bool held = data_.scopeHeld();
    uint32_t steps = keys;
    if (held && view_.waveform().options().mode == plot::Mode::Spectrum && (keys & (Keypad::Left | Keypad::Right))) {
        beginHeldView();
        auto view = view_.waveform().options();
        view.zoomOut = 1; // Zooming FFT bins uses its own 1x base.
        if (keys & Keypad::Right) view.zoom = std::min<uint16_t>(plot::Options::MaximumZoom, view.zoom * 2);
        if ((keys & Keypad::Left) && view.zoom > 1) view.zoom /= 2;
        view_.waveform().configure(view);
        steps &= ~uint32_t(Keypad::Left | Keypad::Right);
    }
    auto request = data_.scopeRequest();
    auto &range = request.frontend.range[selectedChannel_];
    if ((steps & Keypad::Up) && range + 1u < scope::VoltageRanges::Count) ++range; // Ranges ascend with V/div; the factory's Up is range + 1.
    if ((steps & Keypad::Down) && range) --range;
    stepTimebase(request, steps);
    plot::XyMode::constrain(view_.waveform().options(), request);
    if (held && request.same(data_.scopeRequest())) return; // At a limit: nothing to stage.
    const unsigned source = unsigned(request.trigger.channel);
    const auto limits = request.frontend.range[source] == data_.scopeRequest().frontend.range[source]
        ? scope::TriggerLevel::Bounds{} : triggerBounds(request);
    if (!applyFollowing(request, now, held ? ScopeApply::StageIfHeld : ScopeApply::Configure, LevelIntent::Follow, limits)) {
        view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
        return;
    }
    if (held) followNextCapture();
}
void ApplicationController::finishAutoStep(uint32_t now) {
    const uint32_t keys = autoStep_;
    autoStep_ = 0; // Once, whatever it does.
    if (keys && uiReady_ && !remote_ && state_.mode == MeasurementMode::Oscilloscope && !autoBusy()) scaleKeys(keys, now);
}
bool ApplicationController::scopeScreen() const {
    return uiReady_ && !remote_ && !recoveryPending_ && shutdown_ == Shutdown::None && state_.mode == MeasurementMode::Oscilloscope &&
           panel_ == Panel::None && !state_.settingsOpen && !shots_.busy() && !decoderScreen() && !bodeRestoring();
}
// AUTO (issue #96). Quick: a tap starts Auto Setup, a 2 s hold enters Zoom & Pan. Classic: a tap
// resets the pan, a 2 s hold starts Auto Setup. A stroke that began during Auto Setup only cancels
// it (Cancel: its release before the hold, the countdown hidden); its hold, or its release once the
// setup has ended, gives nothing, never Zoom & Pan or a new setup.
void ApplicationController::scopeAutoKeys(const KeyEvents &events, uint32_t now) {
    using Action = ScopeAutoKey::Action;
    const auto action = autoKey_.events(events, now, scopeScreen(), autoBusy());
    if (action == Action::None) return;
    const bool quick = prefs_.scope.keys.automatic == ScopeKeys::Auto::Quick;
    if (action == Action::Cancel || autoBusy()) {
        if (action != Action::Hold) cancelAuto(now); // An idle stroke's hold while a setup runs: nothing.
    } else if (action == Action::Tap) {
        if (quick) startAuto(now);
        else resetPan(now);
    } else if (quick) enterZoom(now);
    else startAuto(now);
    dataRefresh_ = true;
}
int ApplicationController::scopeAutoCountdown(uint32_t now, bool powerHeld) {
    if (powerHeld || !scopeScreen()) autoKey_.cancel(); // REL takes the stroke; a panel or another screen ends it.
    return autoBusy() ? -1 : autoKey_.percent(now); // During Auto Setup a hold does nothing.
}
const char *ApplicationController::scopeAutoHint() const {
    return prefs_.scope.keys.automatic == ScopeKeys::Auto::Quick ? "Hold for Zoom & Pan" : "Hold for auto setup";
}
bool ApplicationController::scopeHoldKeys(uint32_t holds, uint32_t now) {
    holds &= Keypad::F1 | Keypad::F2 | Keypad::F3 | Keypad::F4;
    if (!holds || !scopeScreen()) return false;
    if (autoBusy() || data_.zeroBusy()) return true; // Taken: the stroke is spent, nothing happens.
    followShownChannel();
    syncCursorFocus();
    if (holds & Keypad::F1) openProbe(now);
    else if (holds & Keypad::F2) resetView(now);
    else if (holds & Keypad::F3) triggerMidpoint(now, true); // Level 50 %, as on the factory firmware, and pretrigger 50 % (#123).
    else openShortcut(now);
    dataRefresh_ = true;
    return true;
}
void ApplicationController::openProbe(uint32_t now) {
    channelEditor_.begin(data_.scopeRequest(), view_.waveform().options(), selectedChannel_);
    openPanel(Panel::Channel, channelEditor_, ui::ChannelEditor::Probe);
    fieldKeys(Keypad::Menu, channelEditor_, now); // Its choices open: Up/Down, MENU takes one, F4 keeps the ratio.
}
// The editor of the saved destination, opened for review: nothing is applied until a field is
// committed there, and Back returns to the waveform.
void ApplicationController::openShortcut(uint32_t) {
    using Shortcut = ScopeKeys::Shortcut;
    analyzeRow_ = ~0U;
    switch (const auto destination = prefs_.scope.keys.destination()) {
    case Shortcut::Measurements:
        measurementEditor_.begin(prefs_.scope.measurements);
        openPanel(Panel::Measurements, measurementEditor_);
        return;
    case Shortcut::Acquisition: case Shortcut::RecordMemory:
        acquisitionEditor_.begin(data_.scopeRequest(), view_.waveform().options(), selectedChannel_);
        acquisitionEditor_.snapshot(view_.waveform().model().metadata());
        openPanel(Panel::Acquisition, acquisitionEditor_,
                  destination == Shortcut::RecordMemory ? ui::AcquisitionEditor::Memory : ui::AcquisitionEditor::Timebase);
        return;
    case Shortcut::Interleave: // Settings > Device > Performance, where the board has that control.
        state_.settingsOpen = true;
        settingsMenu_.open(ui::SettingsPage::Performance, ui::SettingsAction::Interleave);
        navigator_.reset(settingsMenu_.selected());
        showSettings();
        return;
    default: // Cursor settings: Analyze > Display > Cursors, its section only.
        for (unsigned c = 0; c < 2; ++c) plotControls_.enabled[c] = channelShown(c);
        plotControls_.options = view_.waveform().options(); plotControls_.channel = selectedChannel_;
        plotControls_.options.cursors.channel = selectedChannel_;
        plotControls_.group = plot::Controls::Group::Cursors; plotControls_.listing = false;
        sectionDirect_ = true; // Back closes the section (analyzeRow_ none).
        openPanel(Panel::Display, ui::DisplayProperties(plotControls_));
        return;
    }
}
// F2 hold, an explicit reset: Home view centres both zero lines too (as Analyze > Center channels);
// both reset the zoom to 1x and the pan to the trigger reference at the time/div in force. In HOLD it
// is also the view RUN returns to, unlike exploring the record.
bool ApplicationController::resetView(uint32_t now) {
    const bool home = prefs_.scope.keys.reset == ScopeKeys::Reset::Home;
    const bool held = data_.scopeHeld();
    if (held) beginHeldView();
    if (home && !centerChannels(now)) return false;
    homeZoom(now);
    view_.notify(Tooltip::Done, home ? "Home view" : "Zoom and pan reset", now);
    dataRefresh_ = true;
    return true;
}
void ApplicationController::homeZoom(uint32_t now) {
    auto options = view_.waveform().options();
    bool changed = plot::ViewZoom::reset(options);
    if (changed) view_.waveform().configure(options);
    if (heldViewActive_) changed |= plot::ViewZoom::reset(heldView_);
    if (changed) queuePlotSave(now);
    dataRefresh_ = true;
}
void ApplicationController::enterZoom(uint32_t now) {
    const auto &model = view_.waveform().model();
    if (!plot::ViewZoom::available(view_.waveform().options(), model)) {
        view_.notify(Tooltip::Unavailable, model.hasFrame() && model.metadata().rolling ? "Zoom & Pan: not in Roll" : "Zoom & Pan needs a Y-T record", now);
        return;
    }
    if (focus_ != plot::Focus::Zoom) zoomReturn_ = focus_;
    setScopeFocus(plot::Focus::Zoom);
}
void ApplicationController::leaveZoom() {
    setScopeFocus(zoomReturn_ == plot::Focus::Zoom ? plot::Focus::Scale : zoomReturn_);
    syncCursorFocus(); // A cursor line that is no longer drawn: Scale.
}
void ApplicationController::syncZoomFocus() {
    if (focus_ == plot::Focus::Zoom && !plot::ViewZoom::available(view_.waveform().options(), view_.waveform().model())) leaveZoom();
}
// Up/Down zoom, Left/Right pan (Settings > Display > Pan direction): the view only. In HOLD they
// explore the retained record (RUN returns the view from before HOLD); in RUN the view is kept.
void ApplicationController::zoomKeys(uint32_t arrows, uint32_t now, bool fine) {
    const int vertical = axis(arrows, Keypad::Down, Keypad::Up);
    if (!vertical) { moveTrace(arrows, now, fine); return; }
    const auto &model = view_.waveform().model();
    if (!viewFollows()) { if (fine) view_.notify(Tooltip::Unavailable, "No record to zoom", now); return; } // As moveTrace.
    auto options = view_.waveform().options();
    const auto step = plot::ViewZoom::step(options, vertical, model);
    if (step == plot::ViewZoom::Step::Unavailable) { view_.notify(Tooltip::Unavailable, "Zoom & Pan needs a Y-T record", now); return; }
    if (step == plot::ViewZoom::Step::Limit) { view_.notify(Tooltip::Unavailable, vertical > 0 ? "Maximum zoom" : "Minimum zoom", now); return; }
    const bool held = data_.scopeHeld();
    if (held) beginHeldView(); // Before the change: what RUN returns to.
    view_.waveform().configure(options);
    if (!held) queuePlotSave(now);
    dataRefresh_ = true;
}
} // namespace openosc
