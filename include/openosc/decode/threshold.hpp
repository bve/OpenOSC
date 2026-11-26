#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::decoding {
// One logic transition of a source: the record index of the first point at the new level.
struct Transition {
    uint32_t index = 0;
    uint8_t level = 0, channel = 0;
};
// A threshold with hysteresis in a record's codes (plan §6.1), from millivolts at the probe tip through
// the channel's scale (FrameView::volts: (code - zero) * voltsPerCode * probe). A rise needs a code at
// least `high`, a fall a code at most `low` (high > low). Not usable when the channel has no scale or
// either level lies outside the ADC's codes: `status` says why.
struct ThresholdCodes {
    enum class Status : uint8_t { Ok, NoScale, OutOfRange };
    int16_t high = 1, low = 0;
    Status status = Status::NoScale;
    static ThresholdCodes of(const scope::ChannelMetadata &channel, int32_t thresholdMv, uint32_t hysteresisMv);
    // The automatic hysteresis (plan §6.1) of a line whose codes span `swing` (highest - lowest):
    // `percent` of that swing in millivolts at the probe tip, rounded to nearest; 0 without a scale.
    static uint32_t autoHysteresisMv(const scope::ChannelMetadata &channel, uint32_t swing, unsigned percent);
    // A line's Auto band: `percent` of the swing its codes span in the record (lowest..highest), at
    // least floorMv (Protocols::autoHysteresisFloorMv), the width used into hysteresisMv. Within the
    // ADC's codes it is of(). A level the floor puts beyond them is the rail when the record reached
    // that rail (a clipped signal still decodes), else a code no point has (-129, 128): the line never
    // takes that level, so the record gives no transition. The threshold itself must be in range.
    static ThresholdCodes automatic(const scope::ChannelMetadata &channel, int32_t thresholdMv, int32_t lowest, int32_t highest,
                                    unsigned percent, uint32_t floorMv, uint32_t &hysteresisMv);
};
// CANH - CANL (plan §8.5) at the probe tips: each channel's codes through its own zero, scale and
// probe, once a record (no floating point per point). Both channels must be calibrated. The table's
// unit is 2^shift microvolts, the smallest power of two that keeps every entry within +-(2^30 - 1):
// then any difference of two entries is within +-(2^31 - 2), exact in int32 (no saturation, no
// wrap). shift is 0 (1 uV) for any scale below 1073 V at the probe tip; a larger one (a x1000 probe)
// only coarsens the unit. The threshold is taken into the same unit conservatively (beginDifference).
struct DifferenceScale {
    int32_t units[2][256]; // By the code's byte (uint8_t of the signed code).
    uint8_t shift = 0;
    static bool of(const scope::FrameMetadata &metadata, DifferenceScale &out);
    // `percent` of a difference swing (in the table's unit, < 2^31) in millivolts, rounded to nearest.
    uint32_t autoHysteresisMv(uint64_t swing, unsigned percent) const;
};
// The swing of one line over a record (plan §6.1, the Auto hysteresis): its lowest and highest
// canonical code (FrameView::Cursor: layout, split history, interleave and bus-glitch resolution, as
// the scalar ThresholdStream reads them), or of the CANH - CANL difference in DifferenceScale units
// (FrameView::PairCursor). Block by block, at most ThresholdStream::Block points a call, any cut of
// the record giving the same range; no buffer, no state beyond the two extremes.
class LevelRange {
public:
    void begin(scope::Channel channel) { *this = LevelRange{}; channel_ = channel; }
    void beginDifference(const DifferenceScale &scale) { *this = LevelRange{}; difference_ = &scale; }
    void scan(const scope::FrameView &frame, uint32_t first, uint32_t n);
    bool known() const { return lowest_ <= highest_; }       // At least one point read.
    uint64_t swing() const { return known() ? uint64_t(int64_t(highest_) - lowest_) : 0; }
    int32_t lowest() const { return lowest_; }
    int32_t highest() const { return highest_; }
private:
    const DifferenceScale *difference_ = nullptr;
    int32_t lowest_ = INT32_MAX, highest_ = INT32_MIN;
    scope::Channel channel_ = scope::Channel(0);
};
// The logic level of one channel of a record, block by block (plan §5.2: at most Block points a call,
// the state kept between calls so any cut of the record gives the same transitions). The first point
// that is clearly high or low sets the initial level (no transition). An optional minimum pulse (in
// points, plan §6.2; 0: off) holds a new level until it has lasted that long: a shorter excursion is
// dropped and counted as a glitch. Codes are read through FrameView::Cursor: the record's layout,
// split history, interleave correction and AdcBusGlitch resolution (the scalar path, the reference).
// With a known level and no pending pulse, a block first goes to acceleration::PlateauScan (SIMD on
// the captured bytes, any origin, wrap and split): a block that provably keeps the level is skipped;
// anything else (a possible crossing, an ADC bus suspect byte, no fast path) takes the scalar path.
// Both give the same transitions.
class ThresholdStream {
public:
    static constexpr uint32_t Block = 128;
    void begin(const ThresholdCodes &codes, uint32_t minPulsePoints, scope::Channel channel);
    // CANH (CH1) - CANL (CH2) of a FrameView::pairs() record against a threshold in millivolts:
    // both codes of a point from one PairCursor word (one index, the same bus correction); points
    // where either channel is at an ADC rail are counted (clipped()), never hidden. Transitions
    // carry `channel` (the logical line's number for its decoder).
    void beginDifference(const DifferenceScale &scale, int32_t thresholdMv, uint32_t hysteresisMv, uint32_t minPulsePoints,
                         uint8_t channel);
    // The points [first, first + n) of the record, n <= Block, following the points fed before;
    // transitions to `out` (room for n + 1), returns how many. `scalarOnly`: the reference path.
    unsigned scan(const scope::FrameView &frame, uint32_t first, uint32_t n, Transition *out, bool scalarOnly = false);
    int level() const { return level_; }           // -1 until the first clear level.
    int initialLevel() const { return initial_; }  // The record's first clear level, -1 if none.
    uint32_t initialIndex() const { return initialAt_; }
    uint32_t glitches() const { return glitches_; }
    uint32_t skippedPoints() const { return skipped_; } // Points the fast path accounted for.
    // A level still held for the minimum pulse when the record ends: not a transition (it never lasted).
    bool pending() const { return pending_; }
    uint32_t clipped() const { return clipped_; } // Difference: points with a channel at an ADC rail.
private:
    __attribute__((always_inline)) void feed(int32_t value, uint32_t index, Transition *out, unsigned &count);
    unsigned scanDifference(const scope::FrameView &frame, uint32_t first, uint32_t n, Transition *out);
    void reset(uint32_t minPulsePoints, uint8_t channel);
    int32_t high_ = 1, low_ = 0; // Codes, or microvolts of the difference.
    const DifferenceScale *difference_ = nullptr;
    uint8_t channel_ = 0;
    uint32_t minPulse_ = 0, pendingAt_ = 0, initialAt_ = 0, glitches_ = 0, skipped_ = 0, clipped_ = 0;
    int level_ = -1, initial_ = -1;
    bool pending_ = false;
};
} // namespace openosc::decoding
