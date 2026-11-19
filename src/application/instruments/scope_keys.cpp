#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/scope_actions.hpp"
#include "openosc/acquisition/timebases.hpp"
#include "openosc/ui/scope/controls.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include <algorithm>

namespace openosc {
namespace {
int axis(uint32_t keys, uint32_t negative, uint32_t positive) {
    return (keys & positive ? 1 : 0) - (keys & negative ? 1 : 0);
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
    view_.scopeContext(selectedChannel_, focus_);
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
void ApplicationController::beginHeldView() {
    if (heldViewActive_) return;
    heldView_ = view_.waveform().options();
    heldViewActive_ = true;
    heldViewRelease_ = data_.scopeHoldReleases();
}
// Issue #7, as on the factory firmware: the time/div is shared by HOLD and RUN. A held record is
// shown at the next capture's time/div, whoever chose it (Left/Right, the Timebase menu, a host),
// and RUN keeps it; the record's metadata, lease and capture are untouched. FFT keeps its own axis.
void ApplicationController::followNextTimebase() {
    const auto &model = view_.waveform().model();
    const bool held = data_.scopeHeld() && model.hasFrame() && model.metadata().timebaseNs &&
                      view_.waveform().options().mode != plot::Mode::Spectrum;
    const uint64_t next = data_.scopeRequest().timebaseNs;
    const uint64_t shown = held && next != model.metadata().timebaseNs ? next : 0;
    if (view_.waveform().options().heldTimebaseNs == shown) return; // Every display pass asks: no copy.
    auto options = view_.waveform().options();
    if (held) { // The chosen scale replaces the zoom, here and after RUN.
        beginHeldView();
        options.zoom = options.zoomOut = 1;
        heldView_.zoom = heldView_.zoomOut = 1;
    }
    options.heldTimebaseNs = shown;
    view_.waveform().configure(options);
    dataRefresh_ = true;
}
void ApplicationController::moveTrace(uint32_t keys, uint32_t now, bool fine) {
    const bool held = data_.scopeHeld();
    if (held) beginHeldView();
    auto request = data_.scopeRequest();
    auto options = view_.waveform().options();
    // "Move waveform": Right shows earlier samples (X-Y: the axis shift keeps its sign).
    int horizontal = axis(keys, Keypad::Left, Keypad::Right) * (prefs_.panReverse && options.mode != plot::Mode::XY ? -1 : 1);
    panRepeats_ = fine || !horizontal ? 0 : panRepeats_ + 1; // Held: the step doubles every PanDoubling repeats, up to 8x.
    if (options.mode != plot::Mode::XY) horizontal *= 1 << std::min(3U, panRepeats_ / PanDoubling);
    const auto change = focus_ == plot::Focus::Trigger
        ? plot::TraceMove::Change{false, plot::TraceMove::pan(options, horizontal, &view_.waveform().model(), fine)}
        : plot::TraceMove::apply(request, options, selectedChannel_, held, horizontal, axis(keys, Keypad::Up, Keypad::Down), fine,
                                 &view_.waveform().model());
    if (change.request && !applyEdited(request, now)) view_.notify("Finish data transfer first", now);
    if (change.view) {
        view_.waveform().configure(options);
        if (!held) queuePlotSave(now);
    }
}
bool ApplicationController::holdKeys(uint32_t keys, uint32_t now) {
    if (!(keys & Keypad::HoldSave)) return false;
    if (!data_.scopeHeld()) data_.holdScope(true);
    else if (!data_.runScope() && uiReady_) view_.notify("Cannot run: the record is in use", now);
    return true;
}
void ApplicationController::scopeKeys(uint32_t keys, uint32_t now, bool fine) {
    followShownChannel(); // Before any key acts on the selection.
    if (keys & Keypad::AutoRange) { // Held: auto setup (autoHold). Short: cancel it, or back to the trigger point.
        if (!autoFired_) { if (autoBusy()) cancelAuto(now); else if (!autoShown_) resetPan(now); } // Shown: a cancelled hold.
        autoFired_ = autoShown_ = false;
        return;
    }
    if (autoBusy()) {
        if (keys & (Keypad::F4 | Keypad::HoldSave)) {
            cancelAuto(now,keys&Keypad::HoldSave);
        }
        return;
    }
    syncCursorFocus();
    const auto openTrigger=[&] {
        triggerEditor_.begin(data_.scopeRequest().trigger);
        triggerEditor_.sources(data_.scopeRequest().frontend.enabled[0], data_.scopeRequest().frontend.enabled[1]);
        triggerEditor_.calibration(data_.scopeMetadata());
        openPanel(Panel::Trigger, triggerEditor_);
    };
    if (keys & Keypad::F3) { // The trigger focus, then each press the next mode; the source is in its menu.
        if (focus_ == plot::Focus::Trigger) { triggerModeStep(now); return; }
        setScopeFocus(plot::Focus::Trigger);
        return;
    }
    if (keys & Keypad::Menu) {
        if (focus_ == plot::Focus::Trigger) { openTrigger(); return; }
        if (focus_ == plot::Focus::Cursor) { openAnalyze(ui::ScopeActions::Cursors); return; }
        channelEditor_.begin(data_.scopeRequest(), view_.waveform().options(), selectedChannel_);
        openPanel(Panel::Channel, channelEditor_);
        return;
    }
    if (keys & Keypad::F4) {
        const auto &cursors = view_.waveform().options().cursors;
        if (focus_ == plot::Focus::Cursor) { // Next line, as on the factory firmware.
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
    const uint32_t arrows = keys & (Keypad::Up | Keypad::Down | Keypad::Left | Keypad::Right);
    if (!arrows) return;
    if (focus_ == plot::Focus::Cursor) { moveCursor(arrows, now, fine); return; }
    if (focus_ == plot::Focus::Trigger && (keys & (Keypad::Up | Keypad::Down))) { // Up raises the level, as on the factory firmware.
        auto request = data_.scopeRequest();
        request.trigger.level = std::clamp<int>(request.trigger.level + ((keys & Keypad::Up) ? 1 : -1), -128, 127);
        if (!applyEdited(request, now)) view_.notify("Finish data transfer first", now);
        return;
    }
    if (focus_ == plot::Focus::Position || focus_ == plot::Focus::Trigger) { // Left/Right move the record in both, as on the factory firmware.
        moveTrace(arrows, now, fine);
        return;
    }
    if (data_.scopeHeld()) { // A held frame cannot be re-acquired: Up/Down scale its view.
        beginHeldView();
        plot::Controls controls;
        controls.options = view_.waveform().options();
        controls.channel = selectedChannel_;
        if (keys & (Keypad::Up | Keypad::Down)) controls.adjust(plot::Controls::Gain, keys & Keypad::Up ? -1 : 1); // Up widens V/div.
        auto &view = controls.options;
        const bool spectrum = view.mode == plot::Mode::Spectrum;
        if (spectrum && (keys & Keypad::Right)) view.zoom = std::min<uint16_t>(256, view.zoom * 2); // FFT: its bins.
        if (spectrum && (keys & Keypad::Left) && view.zoom > 1) view.zoom /= 2;
        view_.waveform().configure(controls.options);
        if (spectrum || !(keys & (Keypad::Left | Keypad::Right))) return;
        // Left/Right stage the next capture's time/div: no capture starts, the record stays held.
        auto request = data_.scopeRequest();
        stepTimebase(request, keys);
        plot::XyMode::constrain(view_.waveform().options(), request);
        if (request.same(data_.scopeRequest())) return; // At 10 ns, 20 s or X-Y's limit.
        if (!applyScope(request, now, ScopeApply::StageIfHeld)) { view_.notify("Finish data transfer first", now); return; }
        followNextTimebase();
        return;
    }
    auto request = data_.scopeRequest();
    auto &range = request.frontend.range[selectedChannel_];
    if ((keys & Keypad::Up) && range < 10) ++range; // Ranges ascend with V/div; the factory's Up is range + 1.
    if ((keys & Keypad::Down) && range) --range;
    stepTimebase(request, keys);
    plot::XyMode::constrain(view_.waveform().options(), request);
    if (!applyScope(request, now)) view_.notify("Finish data transfer first", now);
}
} // namespace openosc
