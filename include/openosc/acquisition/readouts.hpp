#pragma once
#include "openosc/acquisition/measurements.hpp"
#include "openosc/acquisition/record_moments.hpp"

namespace openosc::scope {
// What the numeric readouts are computed over. Screen (default, the factory's way): Vpp/Min/Max
// are the exact extremes of every point in the visible window; the other values come from one
// record sample per display column (or every point when fewer points than columns are visible).
// Record: every point of the whole record, in bounded slices beside the waveform.
enum class ReadoutBasis : uint8_t { Screen, Record };
struct ReadoutSelection {
    static constexpr unsigned PerChannel=4;
    uint32_t values[2][PerChannel]={{0,1,2,8},{0,1,2,8}};
    ReadoutBasis basis=ReadoutBasis::Screen;
    // The Scope's DMM trace (plot::MeterReadouts): readouts of its readings over the visible window,
    // whatever the basis; only MeterKinds. Not the scheduler's (same() leaves them out).
    static constexpr Measurement MeterKinds[]={Measurement::Off,Measurement::Mean,Measurement::Minimum,Measurement::Maximum,
                                               Measurement::PeakToPeak,Measurement::Rms};
    static constexpr bool meterKind(unsigned value) {
        for (auto kind : MeterKinds) if (value==unsigned(kind)) return true;
        return false;
    }
    uint8_t meter[PerChannel]={uint8_t(Measurement::Mean),uint8_t(Measurement::PeakToPeak),uint8_t(Measurement::Minimum),
                               uint8_t(Measurement::Maximum)};
    bool valid() const {
        if (unsigned(basis)>1) return false;
        for (const auto &channel : values) for (auto value : channel)
            if (value>=Measurements::Count) return false;
        for (auto value : meter) if (!meterKind(value)) return false;
        return true;
    }
    // A value is shown for the channel (other than Off and the hardware counter, kept apart).
    bool needs(unsigned channel) const {
        for (auto v : values[channel]) if (v!=unsigned(Measurement::Off) && v!=unsigned(Measurement::CounterFrequency)) return true;
        return false;
    }
    // Frequency, period, duty or pulses: the edge pass is needed.
    bool needsEdges(unsigned channel) const {
        for (auto v : values[channel])
            if (v==unsigned(Measurement::Frequency) || v==unsigned(Measurement::Period) ||
                v==unsigned(Measurement::Duty) || v==unsigned(Measurement::PulseCount)) return true;
        return false;
    }
    // The band is needed: for the edge thresholds, or for Top/Bottom/Amplitude.
    bool needsBand(unsigned channel) const {
        if (needsEdges(channel)) return true;
        for (auto v : values[channel])
            if (v==unsigned(Measurement::Amplitude) || v==unsigned(Measurement::Top) || v==unsigned(Measurement::Bottom)) return true;
        return false;
    }
    bool same(const ReadoutSelection &b) const {
        for (unsigned c=0;c<2;++c) for (unsigned i=0;i<PerChannel;++i) if (values[c][i]!=b.values[c][i]) return false;
        return basis==b.basis;
    }
};
// Computed readouts with where they come from: the record (sequence), the record points they
// cover, the basis and when they were finished, so a result is never taken for another frame's.
struct ReadoutResult {
    Measurements values[2];
    bool computed[2]{};
    ReadoutBasis basis=ReadoutBasis::Screen;
    uint64_t timestampUs=0; // The record's (FrameMetadata::timestampUs): when its points were captured.
    uint32_t sequence=0, first=0, points=0, finishedAt=0;
    bool valid=false;
};
// The Record basis without a long monolithic scan, over one record that the caller keeps leased
// and unchanged between start() and the step that finishes. Every channel that shows a value goes
// through the same record positions together:
//   Survey: the moments of each such channel (DSP blocks) and, only for one whose readouts need it
//           (ReadoutSelection::needsBand), its band (column extremes, SignalBands' columns and
//           rule: Top/Bottom/Amplitude) with its StateLevels sample (the edge thresholds) in the
//           same slice;
//   Edges:  the edge pass of every channel with an active edge scanner (EdgeScanner::walk: one
//           pair-reducer column for both, plateaus accounted for as wholes);
//   Confirm: only for a channel whose first candidate failed while a period dominates
//           (EdgeScanner::retryCandidate), the confirming edge pass;
//   Tone:   only for a channel whose edges do not resolve it (ToneEstimate::wanted), the tone
//           estimate, one channel after the other (at most ToneEstimate::cost positions each).
// percent() counts against the most the passes may take, reserved at start() and released as a
// pass turns out unneeded or shorter: it never goes back.
// A pass no channel needs is skipped. step(budget) advances at most budget record positions in
// all (each position covers every channel of the pass); a column cut by a slice end carries over.
class RecordReadouts {
public:
    void start(const FrameView &frame, const ReadoutSelection &selection);
    // Advances by at most budget record positions; true once the result is complete.
    bool step(const FrameView &frame, uint32_t budget, uint32_t now);
    bool running() const { return running_; }
    uint8_t percent() const;
    uint32_t advanced() const { return done_; } // Record positions stepped so far, every pass.
    uint32_t sequence() const { return sequence_; }
    const ReadoutResult &result() const { return result_; }
    void cancel() { running_ = false; }

private:
    // The view started on, field by field: a same-numbered view of other memory, layout, rate,
    // channel set or calibration is another record.
    bool sameRecord(const FrameView &frame) const;
    enum class Pass : uint8_t { Survey, Edges, Confirm, Tone };
    struct Lane {
        dsp::Moments moments;
        BandEstimate band;
        StateLevels levels;
        int16_t low = 0, high = 0; // The column the last slice cut, if `cut`.
        bool cut = false;
        EdgeScanner edges;
        SignalSummary summary;
        bool shown = false, needsBand = false, needsEdges = false, confirming = false, toning = false;
        void column(const ColumnStats &stats, bool complete);
    };
    bool survey(const FrameView &frame, uint32_t n);
    bool bands(const FrameView &frame, uint32_t n);
    bool walk(const FrameView &frame, uint32_t n);
    void surveyed();
    bool edged(); // The edge passes are over: true when a channel needs the tone pass.
    bool tones(const FrameView &frame, uint32_t &budget); // The tone pass in budget; true once done.
    void finish(const FrameView &frame, uint32_t now);
    FrameMetadata metadata_;
    const uint16_t *banks_[4]{};
    SplitHistory split_;
    ReadoutResult result_;
    Lane lanes_[2];
    ToneEstimate tone_; // Of the first toning channel not done yet (one at a time).
    uint32_t toneSpent_=0; // Positions of tone_ so far, against its reserved cost.
    uint32_t sequence_=0, samples_=0, position_=0, done_=0, total_=0; // done_/total_: record positions.
    Pass pass_=Pass::Survey;
    bool running_=false;
};
} // namespace openosc::scope
