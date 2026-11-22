#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::calibration {
// The relative gain of the two channels on one stable two-level source (GitHub #80): both probe tips
// on it (the 1 kHz reference, a generator square), both channels on at the same V/div, DC. Nothing is
// set up for it: the user's own RUN records are measured, so the source keeps running and every
// setting stays the user's. No value of the source is assumed: only both channels' readings of it.
//
// A channel's two levels are the late halves of its complete plateaus, each the mean within Window
// codes of its histogram peak: the start of a plateau carries the probe's compensation (overshoot or
// a rounded edge), which is reported on its own (compensation()) and never measured as gain. The
// target's correction makes its tip volts equal the other channel's, both read with the scale of
// these records (the factor in force included): a factor for the target's cell (channel, range),
// absolute, so a repeated run measures the same and nothing compounds.
//
// The probes are part of both readings. Swapping them (same ratio setting) and measuring again
// cancels their own ratio error: the geometric mean of both passes is the channels' ratio alone, the
// square root of their quotient the probes' difference.
class ChannelMatch {
public:
    enum class Step : uint8_t { Ready, Measuring, Swap, Review, Failed };
    enum class Issue : uint8_t { None, NoFrame, Setup, Square, Saturated, Span, Flat, Implausible };
    // Late plateau samples per channel and level for one pass, at most this many points of a record.
    static constexpr uint32_t Samples = 16384, FramePoints = 32768;
    static constexpr unsigned MinimumRuns = 4, MaximumRuns = 64; // Complete plateaus per level; per record.
    static constexpr uint32_t RunPoints = 32, SettleMs = 100, TimeoutMs = 10000;
    // One code of 60 is 1.7 %; the plateau's mean resolves well under that with the ADC's noise.
    static constexpr int MinimumSpanCodes = 60, Window = 6, Rail = 125;
    static constexpr double DriftLimit = .005; // Of the span, between the third and fourth quarters.
    void begin(unsigned target, const scope::FrameMetadata &setup, double inForce);
    // The same channels as begin() saw: on, range, coupling, probe and scale (a moved position, a
    // new probe ratio or a changed gain ends the measurement).
    bool sameSetup(const scope::FrameMetadata &metadata) const;
    // Measure (Ready, after Swap, again after Failed): records from SettleMs after `nowUs`.
    void start(uint32_t now, uint64_t nowUs);
    bool canSwap() const;
    void swap(); // Review of the first pass: wait for the probes to be swapped.
    bool fresh(const scope::FrameMetadata &metadata) const { return metadata.timestampUs >= afterUs_; }
    void take(const scope::FrameView &frame);
    void tick(uint32_t now); // Measuring past TimeoutMs: Failed.
    Step step() const { return step_; }
    Issue issue() const { return issue_; }
    unsigned target() const { return target_; }
    unsigned reference() const { return target_ ^ 1; }
    unsigned range() const { return range_; }
    const scope::ChannelMetadata &setup(unsigned channel) const { return setup_[channel & 1]; }
    unsigned passes() const { return passes_; } // Measured: 1, 2 after the swap.
    unsigned percent() const;
    double span(unsigned channel) const { return span_[channel & 1]; } // Codes, the last pass.
    // The start of a plateau over its late level, a fraction of the span (+: overshoot), the last pass.
    double compensation(unsigned channel) const { return compensation_[channel & 1]; }
    // The other channel's tip volts over the target's: the factor its readings are off by.
    double ratio() const;
    double probeDifference() const; // After the swap: the probes' ratio difference (fraction).
    // The ratio's quantization uncertainty (1 sigma, fraction): a level the noise leaves within one
    // code is known to a uniform code's .29 only; 0 when every level is dithered. The last pass.
    double resolution() const { return resolution_; }
    double inForce() const { return inForce_; }
    double proposed() const { return inForce_ * ratio(); }

private:
    // A complete plateau, and the band its level's codes are clamped to for the drift (a one-point bus
    // outlier then moves no mean): the level's side of the record's thresholds, Window past its end.
    struct Run { uint32_t first, last; uint8_t level; int16_t floor, ceiling; };
    // A level of the late halves: the mean within Window codes of its peak, how many lie there, and
    // its rms spread there: under half a code the ADC's noise does not resolve the mean (quantized).
    struct Level { double value = 0, rms = 0; uint32_t inside = 0; };
    bool scan(const scope::FrameView &frame, unsigned channel, uint32_t points);
    void accumulate(const scope::FrameView &frame, unsigned channel, const Run &run);
    bool complete() const;
    void finish();
    Level level(unsigned channel, int from, int to) const;
    void fail(Issue issue) { step_ = Step::Failed; issue_ = issue; }
    scope::ChannelMetadata setup_[2];
    // A frame contributes at most FramePoints to scratch_; each level contributes at most
    // Samples to the late histogram. Both bounds fit a 16-bit bin even if all codes coincide.
    static_assert(FramePoints <= UINT16_MAX && 2 * Samples <= UINT16_MAX);
    uint16_t late_[2][256]{}, scratch_[256]{};
    uint32_t runs_[2][2]{}, lateCount_[2][2]{};
    // Whole codes: 524288-point records of 127 codes fit 32 bits many times over the samples taken.
    int32_t earlySum_[2][2]{};
    uint32_t earlyCount_[2][2]{};
    // Per channel and level, the clamped sums and counts of the late halves' first and last halves:
    // their means differ by the drift of a plateau still moving.
    int32_t quarterSum_[2][2][2]{};
    uint32_t quarterCount_[2][2][2]{};
    double ratio_[2]{1, 1}, span_[2]{}, compensation_[2]{}, inForce_ = 1, resolution_ = 0;
    uint64_t afterUs_ = 0;
    uint32_t startedAt_ = 0, sequence_ = 0;
    unsigned target_ = 1, range_ = 0, passes_ = 0;
    bool hasSequence_ = false;
    Step step_ = Step::Ready;
    Issue issue_ = Issue::None;
};
} // namespace openosc::calibration
