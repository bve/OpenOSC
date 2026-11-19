#include "openosc/application/controller.hpp"
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/usb/instrument_usb.hpp"

namespace openosc {
namespace {
// The firmware's steps of a run-mode change: the data owners, PLL1, then the platform timers
// that are not data owners (backlight PWM, the USB trace sampler in trace builds).
class ClockSteps final : public RunModeSwitch::Steps {
public:
    ClockSteps(DataAccess &data, ReadoutScheduler &readouts, BootGuard *guard, BootConfirmation &confirmation)
        : data_(data), readouts_(readouts), guard_(guard), confirmation_(confirmation) {}
    ClockSuspend suspend() override {
        readouts_.suspend(data_); // A background readout holds a frame lease; give it back first.
        return data_.suspendClockUsers();
    }
    // The 360 MHz profile only with its guard written ("starting"): a crash on it then comes back at
    // 200 MHz (BootGuard). USB is already detached here.
    bool prepare(uint32_t mode) override {
        if (mode != OverclockedRunMode) return true;
        const bool armed = guard_ && guard_->arm();
        Watchdog::progress(); // A finished NOR step, written or not.
        return armed;
    }
    // A deliberate change away from an unconfirmed 360 MHz: "stopped", once, still without USB. A
    // failed write leaves "starting" (conservative). An exit from a confirmed one writes nothing.
    void finished(uint32_t previous, uint32_t active) override {
        if (!guard_ || !confirmation_.left(previous, active, OverclockedRunMode)) return;
        guard_->stop();
        Watchdog::progress();
    }
    bool retime(uint32_t mode) override {
        const bool locked = ClockControl::selectPerformance(mode);
        PowerControl::retime(); // From whatever runs now, also after a failed attempt.
        usbtrace::start();
        return locked && ClockControl::activeMode() == mode;
    }
    bool resume() override { return data_.resumeClockUsers(); }
    uint32_t active() const override { return ClockControl::activeMode(); }
private:
    DataAccess &data_;
    ReadoutScheduler &readouts_;
    BootGuard *guard_;
    BootConfirmation &confirmation_;
};
// The instrument USB device across a run-mode change: de-initialized (D+ released) before PLL1
// and the AHB change; after it follows the clock that runs (InstrumentUsb::followClock) and, where
// that is 200 MHz, is initialized again by InstrumentUsb::poll after the settle interval. From
// SYST:CLOC:MODE this ends the session that sent it; the host opens a new one.
class UsbLink final : public RunModeSwitch::Link {
public:
    explicit UsbLink(InstrumentUsb *usb) : usb_(usb) {}
    bool attached() const override { return usb_ && usb_->attached(); }
    void detach() override { usb_->stop(); }
    void settle(uint32_t active) override {
        if (!usb_) return;
        usb_->followClock(active);
        usb_->reattach(Board::milliseconds()); // Nothing where the clock does not allow it.
    }
private:
    InstrumentUsb *usb_;
};
} // namespace
RunModeSwitch::Result ApplicationController::changeRunMode(uint32_t mode, uint32_t now, bool persist) {
    // Operations that time themselves or hold a file across polls finish first.
    if (autoBusy() || shots_.busy() || imageReader_.busy() || recoveryPending_ || shutdown_ != Shutdown::None)
        return {RunModeSwitch::Outcome::Refused, ClockControl::activeMode()};
    ClockSteps steps(data_, readouts_, guard_, guardConfirmation_);
    UsbLink link(usb_);
    const auto result = RunModeSwitch::change(steps, mode, link);
    const bool runs = result.outcome == RunModeSwitch::Outcome::Applied || result.outcome == RunModeSwitch::Outcome::Unchanged ||
                      result.outcome == RunModeSwitch::Outcome::ResumeFailed;
    if (persist && runs && result.active == mode && prefs_.scope.runMode != mode) {
        prefs_.scope.runMode = mode; // Saved to settings.json at power-off, no restart.
        changed(now);
    }
    // Armed and now running: confirmed after its stable time, like a boot on it.
    if (result.active == OverclockedRunMode && result.outcome != RunModeSwitch::Outcome::Unchanged && runs) guardConfirmation_.armed(now);
    readouts_.reset(data_); // Measurement caches and their timings start again.
    dataRefresh_ = true;
    return result;
}
} // namespace openosc
