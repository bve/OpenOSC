#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/data/units.hpp"

namespace openosc::scope {
// The per-point arrays of a trend, structure of arrays: the live record and the copy on show have this
// one layout. CH codes are the ADC's signed 8-bit ones, the sums add them up (the mean is sum / points
// per point); the DMM values are SI. An empty DMM point has a zero count.
struct TrendPoints {
    static constexpr uint32_t Capacity = 2048;
    int32_t sum[2][Capacity];
    int8_t low[2][Capacity], high[2][Capacity];
    float dmmLow[Capacity], dmmHigh[Capacity], dmmMean[Capacity];
    uint16_t dmmCount[Capacity], dmmMissing[Capacity]; // Readings averaged; NaN readings and gap marks.
};
// The copy of the trend that goes with the published frame (TrendBuffer::publish): what the painter
// draws the band from and the CSV writes. It changes only in DataAccess::poll, never while a frame is
// drawn or a reader holds the frame it belongs to, and is valid for the frame of its `sequence`.
struct TrendView {
    const TrendPoints *points = nullptr;
    uint32_t sequence = 0, size = 0, perPoint = 1; // perPoint: ADC points in each stored one.
    Quantity quantity = Quantity::Unknown;         // Of the DMM values.
    // CH1 min, mean, max; CH2 min, mean, max (volts); DMM min, mean, max (SI): NaN for a channel that
    // is off, an uncalibrated one and a DMM point without a reading.
    bool values(const FrameMetadata &metadata, uint32_t index, float (&out)[9]) const;
};
// A TrendPlot-style record (Fluke): the whole record from its start, the time scale halving when the
// memory fills. It follows Roll at the 20 s/div plan with one ADC point per sample, reading the slices
// RollingBuffer accepts, and keeps per stored point the min, max and mean of each channel and of the DMM.
// When Capacity points are stored, neighbouring pairs merge and each point covers twice the samples.
// The memory is record bank 3, which Roll leaves idle; any other capture overwrites it (forget).
class TrendBuffer {
public:
    static constexpr uint32_t Capacity = TrendPoints::Capacity;
    explicit TrendBuffer(uint16_t *memory);
    // A record from its first point (the DMM readings kept in the header start again too).
    void reset(Quantity quantity);
    // The memory is no longer ours: nothing is drawn or read from it.
    void forget();
    bool active() const { return active_; }
    bool append(const FrameView &frame);
    // The DMM at its place in the point being filled: a value, or NaN for a reading without one.
    void reading(float value);
    void meterGap();
    // The DMM part starts again in `quantity`; the channels keep their record.
    void restartMeter(Quantity quantity);
    uint32_t size() const { return size_; }
    // Counts the points closed and the DMM restarts: the published copy is behind while it differs.
    uint64_t revision() const { return revision_; }
    // The means as a Roll-style frame, for `base`, the Roll history's snapshot (channels, rate, clock).
    FrameView snapshot(const FrameMetadata &base) const;
    // After the store took snapshot() as frame `sequence`: the copy on show catches up with the record.
    void publish(uint32_t sequence);
    const TrendView &shown() const { return shown_; }

private:
    struct Memory;
    void close();
    void halve();
    Memory &memory() const { return *memory_; }
    Memory *memory_;
    TrendView shown_;
    uint32_t size_ = 0, perPoint_ = 1, pending_ = 0, shownSize_ = 0;
    uint64_t revision_ = 0, startUs_ = 0;
    int32_t sum_[2] = {};
    int8_t low_[2] = {}, high_[2] = {};
    // The DMM's open point.
    double dmmSum_ = 0;
    float dmmLow_ = 0, dmmHigh_ = 0;
    uint16_t dmmCount_ = 0, dmmMissing_ = 0;
    Quantity quantity_ = Quantity::Unknown;
    bool active_ = false, started_ = false, copyAll_ = true;
};
} // namespace openosc::scope
