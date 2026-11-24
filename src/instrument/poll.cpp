#include "openosc/instrument/data_access.hpp"
#include "openosc/generator/control_bits.hpp"
#include <algorithm>
#include <utility>

namespace openosc {
void DataAccess::followMeterGenerator() {
    if (!active_ || stopping_ || watchdog_.off() || measurement_ != MeasurementMode::Multimeter) return;
    const uint16_t bits = generator::controlBits(generator_.offConfirmed());
    if (bits == meterGeneratorBits_) return;
    meterGeneratorBits_ = bits;
    frontend_.multimeter(bits);
}
void DataAccess::powerMeter(bool on) {
    if (measurement_ != MeasurementMode::Multimeter) { frontend_.dmmPower(on); return; } // The Scope's DMM trace.
    if (!on) { frontend_.multimeterOff(); return; }
    meterGeneratorBits_ = generator::controlBits(generator_.offConfirmed());
    frontend_.multimeter(meterGeneratorBits_);
}
bool DataAccess::startMeterUart() {
    using Receive = dmm::UartDma::Receive;
    if (!uart_.start(measurement_ == MeasurementMode::Multimeter ? Receive::Dma : Receive::Interrupt)) return false;
    receiveErrors_ = uart_.errors();
    receiveOverflows_ = uart_.overflows();
    return true;
}
// The UART stopped and started again in the foreground (dmm::LinkWatchdog's bounded retries).
void DataAccess::restartDmmUart() {
    dmm_.abandon();
    framer_.reset();
    meter_.interruptCharge(); // Readings are lost across the restart.
    breakMeterTrace();
    if ((!uart_.stop() || !startMeterUart()) && measurement_ == MeasurementMode::Multimeter) status_ = DataStatus::HardwareError;
}
// dmm::LinkWatchdog's decision carried out: a silent chip power-cycled, a deferred power-on, a UART
// restart. In the Scope its status stays the capture's (a failed UART shows as no readings).
void DataAccess::serviceDmmWatchdog(uint32_t now) {
    using Action = dmm::LinkWatchdog::Action;
    const bool meter = measurement_ == MeasurementMode::Multimeter;
    switch (watchdog_.service(now, timeUs_, dmmActivatedAtUs_, uart_.status() == DataStatus::HardwareError,
                              uart_.txStatus() == DataStatus::Busy)) {
    case Action::None: return;
    case Action::RestartUart: restartDmmUart(); return;
    case Action::PowerOn:
        powerMeter(true);
        framer_.reset();
        meter_.interruptCharge();
        dmmActivatedAtUs_ = std::min(dmmActivatedAtUs_, timeUs_);
        if (!startMeterUart() && meter) status_ = uart_.status(); // HardwareError: the bounded retries.
        return;
    case Action::PowerOff:
        if (!uart_.stop()) { watchdog_.stopFailed(timeUs_); if (meter) status_ = DataStatus::HardwareError; return; }
        dmm_.abandon();
        meter_.interruptCharge(); // The DMM chip goes off: no reading until it is back.
        powerMeter(false);
        breakMeterTrace();
        dmmActivatedAtUs_ = timeUs_ + uint64_t(dmm::LinkWatchdog::OffMs) * 1000;
        if (meter) status_ = DataStatus::NoData;
        return;
    }
}
bool DataAccess::receiveFaults() {
    if (receiveErrors_ == uart_.errors() && receiveOverflows_ == uart_.overflows()) return false;
    framer_.reset(); // A lost byte breaks the packet it fell in.
    meter_.interruptCharge(); // Bytes were lost: the ampere-hour run ends here, its charge stays.
    receiveErrors_ = uart_.errors();
    receiveOverflows_ = uart_.overflows();
    return true;
}
bool DataAccess::framePacket(uint8_t (&packet)[dmm::Packet::Bytes], unsigned &budget) {
    uint8_t byte;
    for (; budget && uart_.read(byte); --budget) {
        const uint32_t rejected = framer_.rejected();
        const bool framed = framer_.push(byte, packet);
        if (framer_.rejected() != rejected) meter_.interruptCharge(); // A packet failed its check: a reading lost.
        if (framed) {
            --budget;
            watchdog_.packet(timeUs_);
            return true;
        }
    }
    return false;
}
// The streams: no FAT, no UI and no LVGL. Also run while a refresh waits on DMA2D.
bool DataAccess::referenceAllowed() const {
    return generator::ReferencePolicy::allowed({initialized_, active_, stopping_, clockSuspended_,
                                                measurement_ == MeasurementMode::Oscilloscope, zeroBusy(), serial_.enabled()});
}
void DataAccess::serviceStreams(uint32_t now) {
    timeUs_ += uint64_t(uint32_t(now - previousTick_)) * 1000;
    previousTick_ = now;
    StorageAdmission::service(now); // A NOR SPI stop that failed is tried again: the barrier goes once it holds.
    followMeterGenerator(); // Before the frame starts: an ON asked under the DMM has its bits first.
    generator_.service();
    generator_.reference(referenceAllowed()); // Output::reference keeps the jack quiet after a fault until OFF.
    followBusLoad(); // After a generator fault too: the plans follow the bus load it leaves.
    if (active_ && !stopping_ && measurement_==MeasurementMode::Oscilloscope) counters_.service(now);
    uart_.service(now);
    capture_.service(now);
    store_.service(now);
    serviceRolling(now);
    serviceScopeFrames(now);
}
void DataAccess::serviceBackground(uint32_t now) {
    if (!initialized_) return;
    backgroundPass_=true;
    serviceStreams(now);
    backgroundPass_=false;
    serviceScopeCapture(now); // A finished capture starts the next one now, not after the frame.
}
void DataAccess::poll(uint32_t now) {
    serviceStreams(now);
    if (active_ && !stopping_ && measurement_ == MeasurementMode::Multimeter) {
        if (receiveFaults()) status_ = DataStatus::Overflow;
        uint8_t packet[dmm::Packet::Bytes];
        for (unsigned budget = 256; framePacket(packet, budget);)
            if (meter_.accept(packet, sizeof packet, timeUs_)) {
                status_ = meter_.latest().primary.status;
                const auto &reading = meter_.live().primary.quantity == Quantity::Temperature ? meter_.live() : meter_.latest();
                if (logger_.active()) logger_.append(reading, meter_.userHeld());
                session_.observe(reading, meterFunction());
            }
        meter_.service(timeUs_); // The hold mode's countdown and freshness, with or without packets.
        serviceDmmWatchdog(now);
    }
    if (active_ && !stopping_ && measurement_ == MeasurementMode::Multimeter) dmm_.service(uart_, meter_.latest(), now);
    serviceMeterTrace(now);
    if (dmmClosing_) serviceDmmClose(now); // Shutdown: the owed 21 before the UART stops.
    if (recordPending_ && !stopping_) {
        scope::FrameLease lease;
        if (timeUs_ >= recordDeadlineUs_) { recordStatus_ = DataStatus::Timeout; recordPending_ = false; }
        else if (acquireScope(lease)) {
            ScopeRecorder::Meter meter;
            const scope::TrendView *trend = nullptr;
            bool column = false;
            if (!ScopeRecorder::raw(recordFormat_) && trend_.shown().sequence) {
                // A trend's CSV (and CSV.gz) holds the copy's own min/max/mean rows, the DMM's from the copy, not from the ReadingTrace.
                trend = &trend_.shown();
                meter = meterHeader();
                meter.quantity = trend->quantity;
                column = meterTraceAsked_;
            } else column = !ScopeRecorder::raw(recordFormat_) && meterColumn(lease.view().metadata(), meter);
            if (!recorder_.begin(std::move(lease), recordFormat_, &arena_, column ? &meter : nullptr, trend))
                recordStatus_ = recorder_.status();
            recordPending_ = false;
        }
    }
    const bool recording = recorder_.busy();
    recorder_.service();
    if (recording && !recorder_.busy()) recordStatus_ = recorder_.status();
    logger_.service();
    session_.service(timeUs_); // Time and the commit, also while stopping (beginShutdown ended it).
    if (active_ && !stopping_ && measurement_==MeasurementMode::Multimeter) serial_.service(meter_.latest(),now);
    serviceZero(now);
    serviceScopeCapture(now);
}
} // namespace openosc
