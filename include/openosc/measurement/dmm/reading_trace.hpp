#pragma once
#include "openosc/data/units.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::dmm {
// The Scope's DMM trace (DataAccess::meterReadings): the readings of one quantity in time order, the
// oldest overwritten. Time is DataAccess::nowUs() / 1000 modulo 2^32, the clock of the frames'
// timestamps; the value is in SI, NaN for a reading without one (OL, no number): a gap in the line.
// Capacity holds about 248 s at the ZT703S chip's 3.1 packets/s (HIL 2026-11-23): Roll's longest
// window, 20 s/div x 10 divisions (620 readings), with room to spare.
// The readings lie in ITCM (.meter_trace, zeroed at reset), which no capture and no DMA writes: only
// DataAccess::poll adds to them, never while a frame is drawn. Without that memory (storage nullptr:
// OPENOSC_USB_TRACE builds, whose trace ring takes it) nothing is kept.
class ReadingTrace {
public:
    struct Reading {
        uint32_t ms;
        float value;
    };
    static constexpr size_t Capacity = 768;
    explicit ReadingTrace(Reading *storage) : storage_(storage) {}
    void reset(Quantity quantity) {
        head_ = size_ = 0;
        quantity_ = quantity;
    }
    void push(uint32_t ms, float value) {
        if (!storage_) return;
        storage_[head_] = {ms, value};
        head_ = (head_ + 1) % Capacity;
        if (size_ < Capacity) ++size_;
    }
    size_t size() const { return size_; }
    Reading operator[](size_t oldest) const { return storage_[(head_ + Capacity - size_ + oldest) % Capacity]; }
    Quantity quantity() const { return quantity_; }

private:
    Reading *storage_;
    size_t head_ = 0, size_ = 0;
    Quantity quantity_ = Quantity::Unknown;
};
} // namespace openosc::dmm
