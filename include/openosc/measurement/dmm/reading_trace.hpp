#pragma once
#include "openosc/data/units.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openosc::dmm {
// The Scope's DMM trace (DataAccess::meterReadings): the readings of one quantity in time order, the
// oldest overwritten. Time is DataAccess::nowUs() / 1000 modulo 2^32, the Roll snapshots' clock; the
// value is in SI, NaN for a reading without one (OL, no number): a gap in the line. Capacity holds
// Roll's longest window, 20 s/div x 10 divisions, at the ZT703S chip's 3.1 packets/s (HIL 2026-11-23:
// 620 readings) with room to spare.
// The readings lie in memory that only a Roll capture leaves alone (AcquisitionArena::rollSpare): from
// the start of a capture of another kind valid() is false until a Roll start resets them, and nothing
// reads them meanwhile. A reader that can be interrupted by a capture start (a draw that waits on DMA2D,
// RenderIdle) asks valid() and size() again before each reading.
class ReadingTrace {
public:
    struct Reading {
        uint32_t ms;
        float value;
    };
    static constexpr size_t Capacity = 1024;
    static constexpr size_t Bytes = Capacity * sizeof(Reading);
    explicit ReadingTrace(void *storage) : storage_(static_cast<unsigned char *>(storage)) {}
    void reset(Quantity quantity, uint64_t timebaseNs) {
        head_ = size_ = 0;
        quantity_ = quantity;
        timebaseNs_ = timebaseNs;
        valid_ = true;
    }
    void invalidate() { valid_ = false; }
    bool valid() const { return valid_; }
    void push(uint32_t ms, float value) {
        if (!valid_) return;
        const Reading reading{ms, value};
        std::memcpy(storage_ + head_ * sizeof reading, &reading, sizeof reading);
        head_ = (head_ + 1) % Capacity;
        if (size_ < Capacity) ++size_;
    }
    size_t size() const { return valid_ ? size_ : 0; }
    Reading operator[](size_t oldest) const {
        Reading reading;
        std::memcpy(&reading, storage_ + (head_ + Capacity - size_ + oldest) % Capacity * sizeof reading, sizeof reading);
        return reading;
    }
    Quantity quantity() const { return quantity_; }
    uint64_t timebaseNs() const { return timebaseNs_; }

private:
    unsigned char *storage_;
    size_t head_ = 0, size_ = 0;
    uint64_t timebaseNs_ = 0;
    Quantity quantity_ = Quantity::Unknown;
    bool valid_ = false;
};
} // namespace openosc::dmm
