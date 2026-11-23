#include "openosc/instrument/data_access.hpp"
#include "openosc/platform/board.hpp"
#include <cmath>

// The Scope's DMM trace: the DMM chip powered beside the capture, its packets into the DMM's session,
// and the readings Roll draws.
namespace openosc {
static_assert(dmm::ReadingTrace::Bytes <= scope::AcquisitionArena::BankBytes, "The DMM trace's readings fit bank 3");
// Each pass: started while asked for in a running Scope, stopped otherwise. While it runs, as the DMM:
// the packets into the session, LinkWatchdog (power by bit 13 alone, the UART by interrupt) and the
// function commands. A clock change leaves it running with its UART stopped until restartClockUsers.
void DataAccess::serviceMeterTrace(uint32_t now) {
    if (clockSuspended_) return;
    if (!meterTraceAsked_ || !initialized_ || !active_ || stopping_ || measurement_ != MeasurementMode::Oscilloscope) {
        stopMeterTrace();
        return;
    }
    if (!meterTraceRunning_) {
        meterTraceRunning_ = true;
        ++meterTraceStarts_;
        meterTraceSinceUs_ = timeUs_;
        meterGap_ = true;
        dmm_.abandon();
        framer_.reset();
        uint32_t remainingMs = 0;
        // Unpowered too recently (a DMM left just now): LinkWatchdog powers it once OffMs has passed.
        if (watchdog_.activate(now, timeUs_, remainingMs)) dmmActivatedAtUs_ = timeUs_ + uint64_t(remainingMs) * 1000;
        else {
            dmmActivatedAtUs_ = timeUs_;
            powerMeter(true); // Powered first, then its UART, as the DMM's activation.
            startMeterUart(); // Refused: LinkWatchdog's silence power-cycles the chip.
        }
    }
    receiveFaults();
    uint8_t packet[dmm::Packet::Bytes];
    for (unsigned budget = 256; framePacket(packet, budget);)
        if (meter_.accept(packet, sizeof packet, timeUs_)) traceReading();
    meter_.service(timeUs_);
    serviceDmmWatchdog(now);
    dmm_.service(uart_, meter_.latest(), now);
}
// Its UART first, then the chip's power, as select() leaves the DMM. The chip is unpowered from now
// on (LinkWatchdog::left): the DMM entered next keeps it off for the rest of OffMs.
void DataAccess::stopMeterTrace() {
    if (!meterTraceRunning_) return;
    meterTraceRunning_ = false;
    dmm_.abandon();
    uart_.stop(); // Receive::Interrupt: no stream to refuse the stop.
    frontend_.dmmPower(false);
    watchdog_.left(Board::milliseconds()); // Off already (a power cycle, a deferred power-on): since then.
    watchdog_.deactivate();
    framer_.reset();
    meterGap_ = true;
}
void DataAccess::traceReading() {
    const auto &reading = meter_.live();
    if (reading.sequence != meter_.latest().sequence) return; // Not shown: an overload's settling (OverloadSettle).
    // No Roll since a capture of another kind, HOLD, or the old function's readings while a selection runs.
    if (!meterReadings_.valid() || scopeHeld_ || dmm_.selectionStatus() == DataStatus::Busy) { meterGap_ = true; return; }
    const auto &primary = reading.primary;
    const auto ms = uint32_t(reading.timestampUs / 1000);
    if (primary.quantity != Quantity::Unknown && primary.quantity != meterReadings_.quantity()) {
        meterReadings_.reset(primary.quantity, meterReadings_.timebaseNs());
        meterGap_ = false;
    }
    if (meterGap_) meterReadings_.push(ms, NAN);
    meterGap_ = false;
    meterReadings_.push(ms, primary.valid() ? float(primary.si) : NAN);
}
const dmm::Result *DataAccess::meterTraceReading() const {
    const auto &reading = meter_.live();
    if (!meterTraceRunning_ || !reading.sequence || reading.timestampUs < meterTraceSinceUs_ ||
        timeUs_ - reading.timestampUs > ReadingStaleUs)
        return nullptr;
    return &reading;
}
} // namespace openosc
