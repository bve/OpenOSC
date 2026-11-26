#include "openosc/instrument/data_access.hpp"

namespace openosc {
ClockSuspend DataAccess::suspendClockUsers() {
    if (clockSuspended_) return ClockSuspend::Suspended;
    revokeBackground(); // The background readers yield, as for any change; they ask again.
    inspected_.release(); // DataAccess's own trigger inspection lease, renewed each poll.
    // Every refusal comes before anything stops: a busy instrument keeps running untouched.
    const uint32_t ownLeases = heldFrame_.valid() ? 1U : 0U; // HOLD keeps its record through the change.
    if (!initialized_ || stopping_ || zeroBusy() || recorder_.busy() || recordPending_ || dmmBusy() ||
        uart_.txStatus() == DataStatus::Busy || store_.busy() || retainedRegion_ || store_.readers() != ownLeases ||
        generator_.clockBusy() || // A ZT706S module frame on the wire finishes first (~0.53 s).
        StorageAdmission::active()) // A NOR transfer, or SPI streams not yet proven stopped (PLL and SPI4 must not change under them).
        return ClockSuspend::Busy;
    generatorRetime_ = generator_.enabled();
    serialRetime_ = serial_.enabled();
    // In order; a step that fails starts again what the steps before it stopped, on the unchanged
    // clocks. Busy only if that worked: otherwise the instrument is no longer as it was.
    const auto undo = [&](bool generator, bool measurement) {
        return restartClockUsers(generator, measurement) ? ClockSuspend::Busy : ClockSuspend::Disrupted;
    };
    if (!serial_.stop()) return ClockSuspend::Busy;
    if (!generator_.suspendForClock()) return undo(false, false);
    counters_.stop();
    if (!stopCapture(true) || !uart_.stop()) return undo(true, true);
    active_ = false;
    clockSuspended_ = true;
    ++readoutRevision_; // Timings computed under the old clock are not reused.
    return ClockSuspend::Suspended;
}
bool DataAccess::resumeClockUsers() {
    if (!clockSuspended_) return true;
    clockSuspended_ = false;
    return restartClockUsers(true, true);
}
// The users stopped for a clock change, started again from whatever bus clocks run now.
bool DataAccess::restartClockUsers(bool generator, bool measurement) {
    bool ok = true;
    if (generator) ok = generator_.retime(generatorRetime_) && ok;
    if (measurement) {
        // The Next plan from the timer clock that runs, after the generator is back (a streaming
        // DAC halves the capture's maximum rate): HOLD keeps its record and its rate, but
        // ACQ:SRAT? and the Next card describe the capture RUN would start now. A capture started
        // below replaces it with its own metadata.
        if (!describeNext(configuration_)) describeChannels(configuration_);
        if (measurement_ == MeasurementMode::Oscilloscope) {
            captureFailed_ = false;
            // RUN, Normal, Single and Roll plan their capture from the new clocks; a capture that
            // waits for the memory of a frame still on show or read stays active (activateScope).
            ok = activateScope() && ok;
            // The DMM trace's chip stayed powered: only its UART stopped (baud rate from PCLK1). One
            // that does not start is LinkWatchdog's (its silence power-cycles the chip), not the clock's.
            if (meterTraceRunning_ && !watchdog_.off()) startMeterUart();
        } else ok = activate(measurement_) && ok;
    }
    if (serialRetime_) ok = serial_.start(meter_.latest().sequence) && ok;
    return ok;
}
} // namespace openosc
