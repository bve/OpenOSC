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
// The copy of the trend on show (TrendBuffer::publish): all the painter draws the band from and the CSV
// writes, with what it needs to place the points, so that neither depends on the frame the display model
// holds (that one is taken from the store some time after the copy). The arrays change only in
// DataAccess::poll, never while a frame is drawn or a CSV is written; `sequence` is cleared as soon as
// the record is left or restarted, and the copy is valid while it is not zero.
struct TrendView {
    const TrendPoints *points = nullptr;
    uint32_t sequence = 0;                          // Of the frame published with the copy; 0: no valid copy.
    uint32_t size = 0, perPoint = 1, window = 0;    // Points stored; ADC points in each; points the plot spans.
    Rational rate;                                  // Stored points per second.
    uint64_t timestampUs = 0;                       // Of the middle of the first point, as the frame's.
    uint64_t startUs = 0;                           // Of the record's first sample, on the frames' clock.
    Quantity quantity = Quantity::Unknown;          // Of the DMM values.
    // CH1 min, mean, max; CH2 min, mean, max (volts, by `metadata`'s channels); DMM min, mean, max (SI): NaN
    // for a channel that is off, an uncalibrated one and a DMM point without a reading.
    bool values(const FrameMetadata &metadata, uint32_t index, float (&out)[9]) const;
    // The points of plot column `x` of `width`: the window is right-aligned, the newest point at the right
    // edge; empty (first >= last) before the record's start.
    void column(unsigned x, unsigned width, uint32_t &first, uint32_t &last) const;
    // The columns before the record's start.
    unsigned before(unsigned width) const;
};
// A TrendPlot-style record (Fluke): the whole record from its start, the time per point doubling when the
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
    bool append(const FrameView &frame);
    // The DMM at its place in the point being filled: a value, or NaN for a reading without one.
    void reading(float value);
    void meterGap();
    // The DMM part starts again in `quantity`; the channels keep their record.
    void restartMeter(Quantity quantity);
    uint32_t size() const { return size_; }
    // Counts the points closed and the DMM restarts: the published copy is behind while it differs.
    uint64_t revision() const { return revision_; }
    // The means as a Roll-style frame, for `base`, the Roll history's snapshot (channels, rate, clock). Its
    // window is the record once the points have merged (it spans the plot), Capacity points before.
    FrameView snapshot(const FrameMetadata &base) const;
    // After the store took the frame of snapshot() (`published`: its metadata): the copy on show catches up
    // with the record.
    void publish(const FrameMetadata &published);
    const TrendView &shown() const { return shown_; }

private:
    struct Memory;
    uint32_t windowPoints() const { return perPoint_ == 1 ? Capacity : size_; }
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
