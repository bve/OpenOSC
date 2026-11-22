#include "openosc/application/controller.hpp"

namespace openosc {
void ApplicationController::startAuto(uint32_t now) {
    if (data_.scopeRecording()) {
        if (uiReady_) view_.notify("Finish waveform save first", now);
        return;
    }
    autoWasHeld_ = data_.scopeHeld();
    // The frame on show stays where it is (no copy) for HOLD to return to; trials use their own area.
    if (!data_.retainScope()) {
        if (uiReady_) view_.notify("Cannot retain HOLD frame: release USB waveform",now);
        return;
    }
    autoCancelled_=false; autoStep_ = 0;
    data_.scopeTrials(true);
    data_.holdScope(false);
    autoSetup_.begin(data_.scopeRequest(), selectedChannel_, now);
    if (!data_.configureScope(autoSetup_.request())) autoSetup_.fail();
    if (uiReady_) view_.notify("Auto setup...", now);
}
bool ApplicationController::restoreAuto() {
    if (autoWasHeld_) return data_.restoreHeldScope(autoSetup_.original());
    data_.releaseRetained(); // Running again: the record on show from before is not needed.
    return data_.configureScope(autoSetup_.original());
}
bool ApplicationController::cancelAuto(uint32_t now,bool hold) {
    if (!autoBusy()) return true;
    if (hold) autoWasHeld_=true;
    autoCancelled_=true; autoSetup_.fail();
    if (!restoreAuto()) {
        if (uiReady_) view_.notify("Release USB waveform to restore",now);
        return false; // Keep the original request/record until its reader releases.
    }
    data_.releaseRetained(); autoSetup_.reset(); dataRefresh_ = true;
    if (uiReady_) view_.notify("Auto cancelled", now);
    finishAutoStep(now); // A Scale arrow that cancelled it: its step, now that the restore succeeded.
    return true;
}
void ApplicationController::serviceAuto(uint32_t now) {
    using State = scope::AutoSetup::State;
    if (autoSetup_.state() == State::Idle) return;
    autoSetup_.tick(now);
    if (autoSetup_.active()) {
        scope::FrameLease frame;
        if (data_.acquireScope(frame)) {
            const auto counters=data_.scopeCounters();
            const bool change = autoSetup_.observe(frame.view(),now,&counters);
            frame.release();
            if (change && !data_.configureScope(autoSetup_.request())) autoSetup_.fail();
        }
    }
    if (autoSetup_.active()) return;
    auto result = autoSetup_.state();
    if (result == State::Complete) {
        data_.scopeTrials(false); // The found settings capture records again.
        if (!applyFound(autoSetup_.request(), now)) { data_.scopeTrials(true); autoSetup_.fail(); result = State::ApplyError; }
    }
    if (result != State::Complete) {
        if (!restoreAuto()) return;
    }
    const char *message = autoCancelled_ ? "Auto cancelled" : result == State::Complete ? "Auto setup complete" : result == State::NoSignal ? "Auto: no signal" :
        result == State::Clipped ? "Auto: signal outside range" : result == State::Timeout ? "Auto: no stable capture / timeout" :
        result == State::Uncalibrated ? "Auto needs factory calibration" : "Auto: cannot apply acquisition";
    if (uiReady_) view_.notify(message, now);
    data_.releaseRetained(); autoSetup_.reset(); dataRefresh_ = true;
    finishAutoStep(now); // A Scale arrow that cancelled it: its step, now that the restore succeeded.
}
} // namespace openosc
