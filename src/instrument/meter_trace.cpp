#include "openosc/instrument/data_access.hpp"
#include "openosc/measurement/meter_function.hpp"
#include "openosc/platform/board.hpp"
#include <cmath>

// The Scope's DMM trace: the DMM chip powered beside the capture, its packets into the DMM's session,
// and the readings its line is drawn from.
namespace openosc {
namespace {
#ifndef OPENOSC_USB_TRACE
// ITCM, the CPU's alone: Reset_Handler zeroes it in 64-bit ECC words, no capture or DMA reaches it.
__attribute__((section(".meter_trace"), aligned(8))) dmm::ReadingTrace::Reading meterReadingMemory[dmm::ReadingTrace::Capacity];
// The copy of them a save's dmm column is written from (meterColumn), beside them.
__attribute__((section(".meter_trace"), aligned(8))) ScopeRecorder::MeterPoint meterColumnMemory[dmm::ReadingTrace::Capacity];
#endif
} // namespace
dmm::ReadingTrace::Reading *DataAccess::meterTraceMemory() {
#ifdef OPENOSC_USB_TRACE
    return nullptr; // The USB trace ring takes this ITCM (usb_trace.cpp): the DMM trace keeps no readings.
#else
    return meterReadingMemory;
#endif
}
// Each pass: started while asked for in a running Scope, stopped otherwise. While it runs, as the DMM:
// the packets into the session, LinkWatchdog (power by bit 13 alone, the UART by interrupt) and the
// function and AUTO/RANGE commands. A clock change leaves it running with its UART stopped until restartClockUsers.
void DataAccess::serviceMeterTrace(uint32_t now) {
    if (clockSuspended_) return;
    if (!meterTraceAsked_ || !initialized_ || !active_ || stopping_ || measurement_ != MeasurementMode::Oscilloscope) {
        stopMeterTrace();
        return;
    }
    if (!meterTraceRunning_) {
        meterTraceRunning_ = true;
        ++meterTraceStarts_;
        breakMeterTrace();
        dmm_.abandon();
        framer_.reset();
        meter_.expire(timeUs_); // A reading from before is not current, as on entering the DMM (activate()).
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
    breakMeterTrace();
}
// The readings stop now (the trace stopped or started, the chip power-cycled, its UART restarted): the
// line ends here, not at the next reading (a level held from before the stop or a slope across it).
// HOLD keeps every reading its frame shows: the next reading marks the gap then.
void DataAccess::breakMeterTrace() {
    meterGap_ = true;
    if (!scopeHeld_) meterReadings_.gap(uint32_t(timeUs_ / 1000));
}
// The line in the function selected: started again when that function or its quantity is not the
// line's (its scale, units and cursors with it); the same one (the trace's start, a restart's
// reselection) keeps it.
void DataAccess::restartMeterLine() {
    const auto quantity = dmm::Functions::reading(meterTraceFunction_);
    if (quantity == Quantity::Unknown || (meterTraceFunction_ == meterLineFunction_ && quantity == meterReadings_.quantity())) return;
    meterReadings_.reset(quantity);
    meterGap_ = false;
    meterLineFunction_ = meterTraceFunction_;
}
void DataAccess::traceReading() {
    const auto &reading = meter_.live();
    if (reading.sequence != meter_.latest().sequence) return; // Not shown: an overload's settling (OverloadSettle).
    // HOLD (none of the readings a held frame shows is overwritten), or the old function's readings while a
    // selection runs.
    if (scopeHeld_ || dmm_.selectionStatus() == DataStatus::Busy) { meterGap_ = true; return; }
    if (meterTraceFunction_ != meterLineFunction_) restartMeterLine(); // A selection made under HOLD.
    const auto &primary = reading.primary;
    const auto ms = uint32_t(reading.timestampUs / 1000);
    if (primary.quantity != Quantity::Unknown && primary.quantity != meterReadings_.quantity()) {
        meterReadings_.reset(primary.quantity);
        meterGap_ = false;
    }
    // At a slow timebase an entry is the mean of the readings within its span (ReadingTrace::push).
    const uint32_t span = dmm::ReadingTrace::spanMs(request_.timebaseNs);
    if (meterGap_) meterReadings_.gap(ms, span);
    meterGap_ = false;
    meterReadings_.push(ms, primary.valid() ? float(primary.si) : NAN, span);
}
// A scope CSV's or CSV.gz's dmm column while the DMM trace shows (ScopeRecorder::Meter): the readings
// over `record`, from the one in force at its first point to the last by its last, each with the first
// row it holds from, copied as the write starts (the ring goes on meanwhile) into meterColumnMemory, which
// no capture, trial or gzip stream uses. The USB trace build keeps no readings and no column.
bool DataAccess::meterColumn(const scope::FrameMetadata &record, ScopeRecorder::Meter &meter) {
#ifdef OPENOSC_USB_TRACE
    (void)record; (void)meter;
    return false;
#else
    using Point = ScopeRecorder::MeterPoint;
    if (!meterTraceAsked_ || !record.valid()) return false;
    const double rate = record.samplesPerSecond.value(), last = double(record.samples - 1);
    const uint32_t originMs = uint32_t(record.timestampUs / 1000);
    const double originUs = double(record.timestampUs % 1000);
    uint32_t count = 0;
    for (size_t i = 0; i < meterReadings_.size(); ++i) {
        const auto reading = meterReadings_[i];
        // Its place among the record's points, as plot::MeterAxis places it on the plot.
        const double point = (double(int32_t(reading.ms - originMs)) * 1000 - originUs) * rate / 1e6;
        if (point > last) break;
        if (point <= 0) count = 0; // Of the readings before the record only the last holds at its start.
        const Point held{point <= 0 ? 0U : uint32_t(std::ceil(point)), reading.value};
        meterColumnMemory[count++] = held;
    }
    const auto function = dmm::Functions::identify(meter_.latest());
    meter = {meterColumnMemory, count,
             function < dmm::Function::Unknown ? MeterFunctions[unsigned(function)].key : "unknown", meterReadings_.quantity()};
    return true;
#endif
}
} // namespace openosc
