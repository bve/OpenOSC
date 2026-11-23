#include "openosc/application/controller.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/platform/clocks.hpp"

namespace openosc {
// Remote runs at RemoteClock::Mode (a real change reconnects USB like any run-mode change); the
// user's Standalone profile stays the setting and runs again when Remote ends.
bool ApplicationController::setRemote(bool enabled, const char *left) {
    // Without a UI the keys are locked anyway; leaving Remote then means bringing the display up.
    if (enabled == remote_ && (enabled || uiReady_)) return true;
    if (shots_.busy() || data_.scopeRecording() || data_.dmmBusy() || data_.zeroBusy() || panel_ == Panel::InterleaveMatch) return false;
    const auto now = Board::milliseconds(); if (!cancelAuto(now)) return false;
    if (enabled && panel_ == Panel::Bode) finishBode(now, Tooltip::Done, nullptr); // Restored before the clock and the host change.
    if (enabled && bodeOwns()) return false; // Its restore refused (busy) or unconfirmed: tried again, then Remote.
    if (enabled) {
        if (images_.clearing()) return false;
        // A busy image reader refuses the clock change itself (Refused): nothing is cancelled for a
        // Remote that does not happen.
        const uint32_t previous = ClockControl::activeMode();
        const auto clock = changeRunMode(RemoteClock::Mode, now, false);
        if (!RemoteClock::entered(clock)) {
            if (RemoteClock::rollback(clock, previous)) changeRunMode(previous, now, false);
            return false;
        }
        // The local panels, menus and image view close (their keys are locked); the display stays on.
        imageReader_.cancel(); imageShowing_ = false;
        dropEdit(now);
        panel_ = Panel::None; state_.settingsOpen = false;
        remote_ = true;
        if (uiReady_) { view_.measurement(state_); dataRefresh_ = true; }
    } else {
        if (!uiReady_) { // The headless fallback: the display is brought up now.
            uiReady_ = display_.initialize();
            if (!uiReady_) return false;
            if (!prefs_.useBuiltinTheme) themes_.reload();
            buildView();
            display_.enabled(true);
        }
        remote_ = false;
        const auto clock = changeRunMode(prefs_.scope.runMode, now, false);
        idle_.activity(now); dimmed_=false;
        applyBrightness();
        dataRefresh_ = true; view_.measurement(state_);
        if (!RemoteClock::settled(clock) || clock.active != prefs_.scope.runMode)
            view_.notify(Tooltip::Critical, RunModeSwitch::describe(clock.outcome), now); // Also ResumeFailed at the saved profile.
        else if (left) view_.notify(Tooltip::Done, left, now);
    }
    return true;
}
} // namespace openosc
