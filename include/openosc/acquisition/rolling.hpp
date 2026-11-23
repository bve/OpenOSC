#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::scope {
// Roll's history: the frames' points in order, CH1 in the low byte of a word and CH2 in the high one.
// Averaged (Timebases::averaging past its SampledMaximum), a stored point is the mean of `averaging`
// consecutive points of the frames, its codes rounded half away from zero; the sums run on across
// frames, and the snapshot's points are at that rate, each at the middle of the points it averages.
class RollingBuffer {
public:
    explicit RollingBuffer(uint16_t *storage, size_t capacity) : storage_(storage), capacity_(capacity), limit_(capacity) {}
    bool append(const FrameView &frame);
    bool code(Channel channel, size_t index, uint8_t &output) const;
    FrameView snapshot() const;
    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool reset(size_t capacity, uint32_t averaging = 1) {
        if (!capacity || capacity>65536 || capacity>limit_ || !averaging) return false;
        capacity_=capacity; averaging_=averaging; reset(); return true;
    }
    uint64_t dropped() const { return dropped_; }
    void reset() {
        cursor_ = size_ = 0;
        dropped_ = 0;
        continuous_ = false;
        pending_ = 0; sums_[0] = sums_[1] = 0;
    }

private:
    uint16_t *storage_;
    size_t capacity_, limit_, cursor_ = 0, size_ = 0;
    uint64_t dropped_ = 0;
    FrameMetadata metadata_;
    uint64_t nextTimestampUs_ = 0;
    uint32_t averaging_ = 1, pending_ = 0; // Points per stored point; points in sums_ not yet stored.
    int32_t sums_[2] = {};
    bool continuous_ = false;
};
} // namespace openosc::scope
