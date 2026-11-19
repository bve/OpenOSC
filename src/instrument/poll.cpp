#include "openosc/instrument/data_access.hpp"
#include "openosc/generator/control_bits.hpp"
#include <algorithm>
#include <utility>

namespace openosc {
void DataAccess::followMeterGenerator() {
    if (!active_ || stopping_ || dmmOff_ || measurement_ != MeasurementMode::Multimeter) return;
    const uint16_t bits = generator::controlBits(generator_.offConfirmed());
    if (bits == meterGeneratorBits_) return;
    meterGeneratorBits_ = bits;
    frontend_.multimeter(bits);
}
// A DMM chip silent for DmmSilentUs is power-cycled and its UART restarted; also powers it on after DmmOffMs off.
void DataAccess::serviceDmmWatchdog(uint32_t now) {
    if (dmmOff_) {
        if (uint32_t(now - dmmOffAt_) < DmmOffMs) return;
        dmmOff_ = false;
        meterGeneratorBits_ = generator::controlBits(generator_.offConfirmed());
        frontend_.multimeter(meterGeneratorBits_);
        framer_.reset();
        dmmActivatedAtUs_ = std::min(dmmActivatedAtUs_, timeUs_);
        dmmPacketUs_ = timeUs_;
        if (!uart_.start()) { status_ = uart_.status(); return; }
        receiveErrors_ = uart_.errors();
        receiveOverflows_ = uart_.overflows();
        dmmRecovering_ = dmmTripped_;
        dmmTripped_ = false;
        return;
    }
    const uint64_t silent = DmmSilentUs << std::min<uint32_t>(dmmFutileRestarts_, 4);
    if (timeUs_ - std::max(dmmPacketUs_, dmmActivatedAtUs_) < silent || uart_.txStatus() == DataStatus::Busy) return;
    if (!uart_.stop()) {
        status_ = DataStatus::HardwareError;
        dmmPacketUs_ = timeUs_;
        ++dmmFutileRestarts_;
        return;
    }
    dmm_.abandon();
    frontend_.multimeterOff();
    dmmOff_ = dmmTripped_ = true;
    dmmOffAt_ = now; dmmLeft_ = true; // A trip is a power-off: a quick return still waits DmmOffMs.
    dmmActivatedAtUs_ = timeUs_ + uint64_t(DmmOffMs) * 1000;
    ++dmmFutileRestarts_;
    status_ = DataStatus::NoData;
}
// The streams: no FAT, no UI and no LVGL. Also run while a refresh waits on DMA2D.
void DataAccess::serviceStreams(uint32_t now) {
    timeUs_ += uint64_t(uint32_t(now - previousTick_)) * 1000;
    previousTick_ = now;
    StorageAdmission::service(now); // A NOR SPI stop that failed is tried again: the barrier goes once it holds.
    followMeterGenerator(); // Before the frame starts: an ON asked under the DMM has its bits first.
    generator_.service();
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
    serviceStreams(now);
    serviceScopeCapture(now); // A finished capture starts the next one now, not after the frame.
}
void DataAccess::poll(uint32_t now) {
    serviceStreams(now);
    if (active_ && !stopping_ && measurement_ == MeasurementMode::Multimeter) {
        if (receiveErrors_ != uart_.errors() || receiveOverflows_ != uart_.overflows()) {
            framer_.reset();
            receiveErrors_ = uart_.errors();
            receiveOverflows_ = uart_.overflows();
            status_ = DataStatus::Overflow;
        }
        uint8_t byte, packet[dmm::Packet::Bytes];
        for (unsigned n = 0; n < 256 && uart_.read(byte); ++n) {
            if (!framer_.push(byte, packet)) continue;
            dmmPacketUs_ = timeUs_; dmmFutileRestarts_ = 0;
            if (dmmRecovering_) { dmmRecovering_ = false; ++dmmRestarts_; }
            if (dmmReselect_) { dmmReselect_ = false; ++dmmReselects_; }
            if (meter_.accept(packet, sizeof packet, timeUs_)) {
                status_ = meter_.latest().primary.status;
                if (logger_.active())
                    logger_.append(meter_.latest(), meter_.userHeld());
            }
        }
        meter_.service(timeUs_); // The hold mode's countdown and freshness, with or without packets.
        serviceDmmWatchdog(now);
    }
    if (active_ && !stopping_ && measurement_ == MeasurementMode::Multimeter) dmm_.service(uart_, meter_.latest(), now);
    if (dmmClosing_) serviceDmmClose(now); // Shutdown: the owed 21 before the UART stops.
    if (recordPending_ && !stopping_) {
        scope::FrameLease lease;
        if (timeUs_ >= recordDeadlineUs_) { recordStatus_ = DataStatus::Timeout; recordPending_ = false; }
        else if (acquireScope(lease)) {
            if (!recorder_.begin(std::move(lease), recordFormat_))
                recordStatus_ = recorder_.status();
            recordPending_ = false;
        }
    }
    const bool recording = recorder_.busy();
    recorder_.service();
    if (recording && !recorder_.busy()) recordStatus_ = recorder_.status();
    logger_.service();
    if (active_ && !stopping_ && measurement_==MeasurementMode::Multimeter) serial_.service(meter_.latest(),now);
    serviceZero(now);
    serviceScopeCapture(now);
}
} // namespace openosc
