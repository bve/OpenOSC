#pragma once
#include "openosc/data/units.hpp"
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace openosc::dmm {
// The Scope's DMM trace (DataAccess::meterReadings): the readings of one quantity in time order, the
// oldest overwritten. Time is DataAccess::nowUs() / 1000 modulo 2^32, the clock of the frames'
// timestamps; the value is in SI, NaN for a reading without one (OL, no number): a gap in the line.
// Every reading counts: one that comes within spanMs (a screen of 10 divisions over Window, so the
// entries of one screen fit with room for gaps) of the newest entry's first reading, of its kind (a
// value or a gap), joins that entry as the mean of their values at the middle of their times. At
// 20 s/div that span is shorter than the ZT703S chip's 3.1 packets/s (HIL 2026-11-23): each reading
// is an entry of its own, as at every faster timebase.
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
    static constexpr uint32_t Window = 640; // Entries a screen spans at most.
    // The time one entry may cover at a timebase: a screen of 10 divisions over Window.
    static constexpr uint32_t spanMs(uint64_t timebaseNs) { return uint32_t(timebaseNs / 100000 / Window); }
    explicit ReadingTrace(Reading *storage) : storage_(storage) {}
    void reset(Quantity quantity) {
        head_ = size_ = 0;
        count_ = 0;
        quantity_ = quantity;
    }
    void push(uint32_t ms, float value, uint32_t spanMs = 0) {
        if (!storage_) return;
        const bool gap = value != value;
        if (count_ && gap == gap_ && ms - first_ < spanMs) {
            ++count_;
            if (!gap) sum_ += value;
            storage_[(head_ + Capacity - 1) % Capacity] = {first_ + (ms - first_) / 2, gap ? value : float(sum_ / count_)};
            return;
        }
        storage_[head_] = {ms, value};
        head_ = (head_ + 1) % Capacity;
        if (size_ < Capacity) ++size_;
        first_ = ms; count_ = 1; sum_ = gap ? 0 : value; gap_ = gap;
    }
    // The readings stop at `ms` (a gap from there): the newest entry with a value is followed by a gap.
    void gap(uint32_t ms, uint32_t spanMs = 0) {
        if (size_ && !gap_) push(ms, NAN, spanMs);
    }
    size_t size() const { return size_; }
    Reading operator[](size_t oldest) const { return storage_[(head_ + Capacity - size_ + oldest) % Capacity]; }
    Quantity quantity() const { return quantity_; }

private:
    Reading *storage_;
    size_t head_ = 0, size_ = 0;
    uint32_t first_ = 0, count_ = 0; // The newest entry's first reading and its readings (0: none to join).
    double sum_ = 0;                 // Their values' sum (a gap's: none).
    Quantity quantity_ = Quantity::Unknown;
    bool gap_ = false;               // The newest entry is a gap.
};
} // namespace openosc::dmm
