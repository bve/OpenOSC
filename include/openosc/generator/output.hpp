#pragma once
#include "openosc/generator/waveform.hpp"
#include "openosc/platform/resources.hpp"

namespace openosc::generator {
// Whom the DAC stream's bus load concerns (Q-14): told before the stream's first BDMA request, on
// every start (enable, a configure or retime while enabled). False keeps the stream off (Busy).
class BusClient {
public:
    virtual bool dacStreamStarting() = 0;
protected:
    ~BusClient() = default;
};
class Output {
public:
    explicit Output(PeripheralResources &resources) : resources_(resources) {}
    ~Output() { disable(); }
    void attach(BusClient &client) { client_ = &client; }
    bool configure(const Settings &settings);
    bool enable();
    void disable();
    void service();
    // The calibration reference (GitHub #32) while the output is OFF: `allowed` is the instrument's
    // policy each pass (DataAccess::referenceAllowed: Scope, nothing stopping or calibrating, the jack
    // not the DMM serial output's). It runs TIM13 only (no DAC, no BDMA: no bus load) on a lease of the
    // jack alone; enable() takes the jack over, disable() and every stop end it, and a new pass may
    // start it again. OFF stays OFF for the user: enabled(), offConfirmed() and the settings are the
    // user's; the reference never changes them. After a fault, a failed halt or a refused ON nothing
    // starts by itself: the jack stays quiet until the user's explicit disable(); a clock change
    // (suspendForClock) keeps that.
    void reference(bool allowed);
    bool referenceActive() const { return referenceOn_; }
    // After a core clock change, with the output disabled: the timer plan is computed again from
    // the new APB1 timer clock for the same settings, and the output enabled again if `enable`.
    // A setting with no plan at the new clock (above its DAC limit) stays off with fault Limit.
    // The clock is read here (also for retime(false): the limits and timerHz follow the new clock).
    bool retime(bool enable) { readClock(); configured_ = false; return !enable || this->enable(); }
    // A run-mode change: TIM6/TIM13 run from PLL1, so the output stops and retime() plans it again.
    bool suspendForClock() { const bool held = referenceHeld_; disable(); referenceHeld_ = held; return !enabled_; }
    bool clockBusy() const { return false; }
    bool offConfirmed() const { return !enabled_; } // The DAC/PWM halted is off.
    // Started and running: not after a halt that failed (BDMA would not stop: the state is unknown).
    bool onConfirmed() const { return enabled_ && status_ == DataStatus::Ok; }
    bool enabled() const { return enabled_; }
    bool busy() const { return false; }
    bool recovering() const { return false; } // No retries of its own: a start goes through or is refused at once.
    // BDMA to the DAC shares the AHB with the GPIOD reads: at full speed requests are lost and the
    // record overruns at no fixed DAC rate (1 kHz fails, 1.5 kHz does not), so capture slows down
    // whenever the DAC streams. PWM output needs no DMA.
    bool loadsBus() const { return enabled_ && !plan_.pwm; }
    DataStatus status() const { return status_; }
    const Settings &settings() const { return settings_; }
    const OutputPlan &plan() const { return plan_; }
    // `timerHz`: the APB1 timer clock TIM6 runs at, which the DAC limits follow (DacCapabilities). Read
    // at the boundaries (configure, enable, retime, the reference's start), not by each snapshot.
    Snapshot snapshot() const { return {settings_, plan_, status_, enabled_, timerHz_, faults_, fault_}; }
    uint32_t timerHz() const { return timerHz_; }

private:
    bool readClock();
    bool apply();
    bool halt();
    void releaseStopped();
    bool startReference();
    void stopReference();
    void stopped(OutputFault fault); // A stop the output made by itself: counted, for one report each.
    PeripheralResources &resources_;
    BusClient *client_ = nullptr;
    PeripheralLease lease_;
    Settings settings_;
    OutputPlan plan_;
    unsigned activeBank_ = 0;
    bool configured_ = false, enabled_ = false;
    PeripheralLease referenceLease_; // The jack alone, while the reference drives it.
    bool referenceOn_ = false;
    bool referenceHeld_ = false; // A fault or refusal since the last explicit OFF: no reference.
    uint32_t timerHz_ = 0; // The clock last read at a boundary (readClock).
    DataStatus status_ = DataStatus::NoData;
    uint32_t faults_ = 0;
    OutputFault fault_ = OutputFault::None;
};
} // namespace openosc::generator
