#include "openosc/application/controller.hpp"
#include "openosc/ui/scope/meter_readouts.hpp"
#include "openosc/ui/scope/meter_scale.hpp"

namespace openosc {
// AUTO (the key, SCPI AUToscale): the DMM trace's units/div when it is shown (fitMeterScale), then Auto
// Setup of the channels; with no channel shown (the DMM trace alone) the DMM trace's scale only, its
// hidden time axis and the timebase as they are.
bool ApplicationController::startAuto(uint32_t now) {
    if (data_.scopeRecording()) {
        if (uiReady_) view_.notify(Tooltip::Unavailable, "Finish waveform save first", now);
        return false;
    }
    const bool meter = fitMeterScale(now);
    if (view_.waveform().options().meter.shown && !channelShown(0) && !channelShown(1)) {
        if (!meter && uiReady_) view_.notify(Tooltip::Unavailable, "No DMM readings on screen", now);
        return meter;
    }
    autoWasHeld_ = data_.scopeHeld();
    // The frame on show stays where it is (no copy) for HOLD to return to; trials use their own area.
    if (!data_.retainScope()) {
        if (uiReady_) view_.notify(Tooltip::Unavailable, "Cannot retain HOLD frame: release USB waveform",now);
        return false;
    }
    autoCancelled_=false; autoStep_ = 0;
    data_.scopeTrials(true);
    data_.holdScope(false);
    autoSetup_.begin(data_.scopeRequest(), selectedChannel_, now);
    if (!data_.configureScope(autoSetup_.request())) autoSetup_.fail();
    if (uiReady_) view_.notify(Tooltip::Progress, "Auto setup...", now);
    return autoBusy();
}
// The smallest units/div at which the DMM trace's readings in the window fit the plot at its position
// (MeterScale::fit; the largest when none does). False without a reading there to fit.
bool ApplicationController::fitMeterScale(uint32_t now) {
    auto options = view_.waveform().options();
    const auto &readings = data_.meterReadings();
    if (!options.meter.shown || !plot::MeterScale::scaled(readings.quantity())) return false;
    plot::MeterReadouts window;
    window.calculate(readings, view_.waveform().model());
    if (!window.valid) return false;
    const unsigned index = plot::MeterScale::index(readings.quantity());
    int8_t step = 0;
    const double zero = double(options.meter.position) / plot::MeterTraceOptions::PositionSteps;
    if (!plot::MeterScale::fit(index, window.minimum, window.maximum, zero, step) && uiReady_) {
        // Their span fits the plot on its largest scale: the zero's position is what leaves no room.
        const bool span = window.maximum - window.minimum <= 8 * plot::MeterScale::perDivision(plot::MeterScale::Spans[index].highest);
        view_.notify(Tooltip::Advisory, span ? "DMM zero leaves no room: move it" : "DMM readings beyond its largest scale", now);
    }
    if (step != options.meter.scales[index]) {
        options.meter.scales[index] = step;
        view_.waveform().configure(options);
        queuePlotSave(now);
    }
    view_.scopeMeter(meterCard());
    dataRefresh_ = true;
    return true;
}
bool ApplicationController::restoreAuto() {
    if (autoWasHeld_) {
        if (!data_.restoreHeldScope(autoSetup_.original())) return false;
        // This release was only for the Auto trials. The same HOLD view stays open.
        if (heldViewActive_) heldViewRelease_ = data_.scopeHoldReleases();
        return true;
    }
    data_.releaseRetained(); // Running again: the record on show from before is not needed.
    return data_.configureScope(autoSetup_.original());
}
bool ApplicationController::cancelAuto(uint32_t now,bool hold) {
    if (!autoBusy()) return true;
    if (hold) autoWasHeld_=true;
    autoCancelled_=true; autoSetup_.fail();
    if (!restoreAuto()) {
        if (uiReady_) view_.notify(Tooltip::Critical, "Release USB waveform to restore",now);
        return false; // Keep the original request/record until its reader releases.
    }
    data_.releaseRetained(); autoSetup_.reset(); dataRefresh_ = true;
    if (uiReady_) view_.notify(Tooltip::Done, "Auto cancelled", now);
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
        else homeZoom(now); // Issue #111: the signal it found, whole (Quick tap and Classic hold); a cancel or failure keeps the view.
    }
    if (result != State::Complete) {
        if (!restoreAuto()) return;
    }
    const char *message = autoCancelled_ ? "Auto cancelled" : result == State::Complete ? "Auto setup complete" : result == State::NoSignal ? "Auto: no signal" :
        result == State::Clipped ? "Auto: signal outside range" : result == State::Timeout ? "Auto: no stable capture / timeout" :
        result == State::Uncalibrated ? "Auto needs factory calibration" : "Auto: cannot apply acquisition";
    // Done or cancelled; a signal Auto could not set up for; no calibration, or a setup not applied.
    const Tooltip kind = autoCancelled_ || result == State::Complete ? Tooltip::Done
        : result == State::NoSignal || result == State::Clipped || result == State::Timeout ? Tooltip::Advisory : Tooltip::Critical;
    if (uiReady_) view_.notify(kind, message, now);
    data_.releaseRetained(); autoSetup_.reset(); dataRefresh_ = true;
    finishAutoStep(now); // A Scale arrow that cancelled it: its step, now that the restore succeeded.
}
} // namespace openosc
