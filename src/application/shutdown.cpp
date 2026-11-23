#include "openosc/application/controller.hpp"
#include "openosc/application/decode_csv_file.hpp"
#include "openosc/application/bode_csv_file.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "openosc/platform/recovery_request.hpp"
#include "openosc/ui/editors/confirmation.hpp"
#include "openosc/usb/instrument_usb.hpp"

namespace openosc {
void ApplicationController::confirmShutdown(ShutdownAction action) {
    confirmAction_=action;
    confirmOperation(ui::Confirmation(action),&ApplicationController::requestShutdown);
}
// Headless (no LVGL: the REL/Power hold works there too) nothing is drawn; a refused restart then
// shows only in its result (still running, USB back).
void ApplicationController::shutdownNotice(const char *text, uint32_t now) {
    if (uiReady_ && text) view_.notify(Tooltip::Critical, text, now);
}
// The controller's side of ShutdownSequence: what each step does on this instrument.
class ApplicationController::ShutdownSteps final : public ShutdownSequence::Steps {
public:
    static constexpr const char *DecodeCsvShutdown = "DECODE CSV CANCELLED: SHUTDOWN";
    static constexpr const char *BodeCsvShutdown = "FRA CSV CANCELLED: SHUTDOWN";
    explicit ShutdownSteps(ApplicationController &a) : a_(a) {}
    // Every background file user ends before the saves and the unmount: an unfinished decoded or FRA CSV
    // is closed and its .TMP removed now (its export ended), so nothing writes the volume after it.
    void prepare() override { a_.imageReader_.cancel(); a_.images_.cancelClear(); cancelCsvFiles(); }
    bool screenshotBusy() override { return a_.shots_.busy(); }
    void cancelScreenshot() override { a_.shots_.cancel(); }
    bool saveTheme() override { SettingsStore files(a_); return a_.saves_.flushTheme(files); }
    // The user's last confirmed output first (a confirmed OUTP ON in the same batch); the stop that follows
    // (stopInstruments) is the instrument's own and changes nothing kept.
    bool savePreferences() override {
        a_.followGeneratorOutput(Board::milliseconds());
        SettingsStore files(a_); return a_.saves_.flushSettings(files);
    }
    // USB first: the unconfirmed 360 MHz guard's "stopped" (one attempt) is written without a host, while
    // the volume is still mounted (BootGuard).
    void stopInstruments() override { if (a_.usb_) a_.usb_->stop(); a_.stopBootGuard(); a_.data_.beginShutdown(); }
    bool instrumentsStopped() override { return a_.data_.shutdownReady(); }
    void cancelRecorders() override {
        a_.data_.scopeRecorder().cancel(true); a_.data_.meterLogger().cancel(true); // Out of time: the .TMPs stay.
        cancelCsvFiles(); // Already ended in prepare(): idempotent.
    }
    // Finished steps, failed or not (each may end after a NOR timeout): watchdog progress.
    bool unmount() override { const bool done = a_.fs_.unmount(); Watchdog::progress(); return done; }
    bool restoreAddressing() override { const bool done = a_.nor_.restoreLegacyAddressing(); Watchdog::progress(); return done; }
    void notice(const char *text) override { a_.shutdownNotice(text, Board::milliseconds()); }
    void finish(ShutdownAction action) override {
        if (action == ShutdownAction::PowerOff) PowerControl::powerOff();
        if (action == ShutdownAction::Restart) NVIC_SystemReset();
        RecoveryRequest::restart(); // Recovery re-initialises the NOR itself and repairs the volume.
    }
    bool resume() override;
private:
    ApplicationController &a_;
    static void cancelCsvFiles() {
        decodeCsvFile().cancel(DecodeCsvShutdown);
        bodeCsvFile().cancel(BodeCsvShutdown);
    }
};
// RefusedRestart's steps on this instrument: every clock user is still stopped (beginShutdown).
class ApplicationController::RefusedRestartSteps final : public RefusedRestart::Steps {
public:
    explicit RefusedRestartSteps(ApplicationController &a) : a_(a) {}
    bool mount() override { return a_.fs_.mountPrepared(); }
    bool guardedRuns() override { return ClockControl::activeMode() == OverclockedRunMode; }
    bool arm() override { const bool armed = a_.guard_ && a_.guard_->arm(); Watchdog::progress(); return armed; }
    bool retimeSafe() override {
        const bool locked = ClockControl::selectPerformance(0);
        PowerControl::retime(); // From whatever runs now, also after a failed attempt.
        usbtrace::start();
        return locked && ClockControl::activeMode() == 0;
    }
    bool resumeUsers() override { return a_.data_.resume(); }
    void startUsb() override {
        if (!a_.usb_) return;
        a_.usb_->followClock(ClockControl::activeMode());
        a_.usb_->start();
    }
private:
    ApplicationController &a_;
};
bool ApplicationController::ShutdownSteps::resume() {
    RefusedRestartSteps steps(a_);
    const auto result = RefusedRestart::resume(a_.guardStopped_, steps);
    a_.guardStopped_ = false;
    switch (result.guard) {
    case RefusedRestart::Guard::Untouched: break;
    case RefusedRestart::Guard::Armed: a_.guardConfirmation_.armed(Board::milliseconds()); break;
    case RefusedRestart::Guard::Safe: a_.guardNotice_ = "Restart refused; 360 MHz guard unavailable: 200 MHz active"; break;
    case RefusedRestart::Guard::SafeFailed: a_.guardNotice_ = "Restart refused; clock failure: running on HSI, restart"; break;
    }
    a_.panel_ = Panel::None;
    if (a_.uiReady_) a_.view_.measurement(a_.state_);
    a_.dataRefresh_ = true;
    return result.resumed;
}
void ApplicationController::requestShutdown(uint32_t now) {
    if (panel_ == Panel::Bode) finishBode(now, Tooltip::Done, nullptr); // Every source of a shutdown: the sweep ends, restored.
    ShutdownSteps steps(*this);
    shutdownSequence_.begin(confirmAction_, now, steps);
    shutdown_ = shutdownSequence_.action();
}
void ApplicationController::serviceShutdown(uint32_t now) {
    if (!shutdownSequence_.active()) return;
    ShutdownSteps steps(*this);
    shutdownSequence_.service(now, steps);
    shutdown_ = shutdownSequence_.action();
    // Shown over the refusal's own line: the clock the instrument now runs matters more.
    if (guardNotice_ && !shutdownSequence_.active()) { shutdownNotice(guardNotice_, now); guardNotice_ = nullptr; }
}
} // namespace openosc
