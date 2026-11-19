#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::scope {
enum class TriggerMode : uint8_t { Auto, Normal, Single };
enum class Edge : uint8_t { Rising, Falling };
struct TriggerSettings {
    Channel channel = Channel::Ch1;
    Edge edge = Edge::Rising;
    TriggerMode mode = TriggerMode::Auto;
    int16_t level = 0;
    uint16_t position=250; // Requested pre-trigger fraction, in thousandths of the record.
    // Noise reject: an edge counts only if, since the signal was last past the level, it went
    // RejectCodes beyond the level on the side it starts from (hysteresis on arming; the edge stays
    // where it crosses the level). Off counts every crossing: chatter on the opposite slope can
    // then trigger half a period late.
    bool reject = true;
    static constexpr int RejectCodes = 4;
    bool valid() const;
    // The code a rising (falling) edge must have come from at or below (above): the level itself
    // without reject, else RejectCodes beyond it, within the ADC's codes.
    int armLevel() const;
};
// Edges of one channel of a record: sample i-1 on or below (above) the level, sample i above
// (below), and, with reject, armed (TriggerSettings::armLevel) within the run on the starting side
// that ends at i-1 (seen in the record: a run reaching its first point is unarmed unless armed in it).
class TriggerSearch {
public:
    static bool find(const FrameView &frame, const TriggerSettings &settings, uint32_t &index);
    // Edge `index`: where the level (between codes, level +- 0.5) is crossed, in 1/65536 points before it.
    static uint32_t crossingBeforeQ16(const FrameView &frame, const TriggerSettings &settings, uint32_t index);
    static bool nearest(const FrameView &frame,const TriggerSettings &settings,uint32_t target,uint32_t &index);
    // The same among the edges at indices first..last only (clamped to the record): ties to the lower index.
    static bool nearest(const FrameView &frame,const TriggerSettings &settings,uint32_t target,uint32_t first,uint32_t last,
                        uint32_t &index);
};
class TriggerCache {
public:
    bool find(const FrameView &frame, const TriggerSettings &settings, uint32_t &index);
    // A result already proved for this frame and these settings (the capture's own search over a
    // ring holding the whole frame): a later find() with both unchanged does not scan again.
    void remember(const FrameView &frame, const TriggerSettings &settings, bool found, uint32_t index);

private:
    bool matches(const FrameView &frame, const TriggerSettings &settings) const;
    TriggerSettings settings_;
    uint64_t timestamp_ = 0;
    uint32_t sequence_ = 0, samples_ = 0, origin_ = 0, ring_ = 0, index_ = 0;
    Layout layout_ = Layout::Linear;
    uint8_t channels_ = 0;
    bool valid_ = false, found_ = false;
};
// Where a stated frequency comes from. Edges: the periods of the edge pass (EdgeScanner), at four
// points per cycle or more. Spectrum: the dominant tone (ToneEstimate) of a signal faster than
// that, where the edges miss cycles: its period is 1/f, its duty and pulse count are unknown.
enum class FrequencySource : uint8_t { None, Edges, Spectrum };
struct SignalSummary {
    int16_t minimum = 0, maximum = 0;
    int16_t robustMinimum = 0, robustMaximum = 0; // The band (BandEstimate) when hasRobust.
    bool hasRobust = false;
    double mean = 0, rms = 0, frequencyHz = 0, dutyPercent = 0;
    // Edges: the samples between the first and the last rising crossing counted (the frequency's
    // resolution is about one sample in it) and the share of the periods that agreed; 0 otherwise.
    double frequencySpan = 0, frequencyAgreement = 0;
    uint32_t pulses = 0;
    bool hasPulses = true; // False when the edges were not resolved (Screen basis), so n is unknown.
    bool valid = false, hasFrequency = false, hasDuty = false;
    FrequencySource frequencySource = FrequencySource::None;
};
// Hysteresis edges between 30 % and 70 % of a band (the readouts give the StateLevels of SignalBands'
// robust one, AUTO that band itself), crossings interpolated at 50 %: frequency, duty and pulses. finish() states a frequency and a
// duty only for a record whose periods (rising crossing to rising crossing) mostly agree: the
// median of the first ConsensusPrefix periods is the candidate, and at least three quarters of ALL
// periods must lie within 20 % of it. If every period does, the values are those of all of them;
// otherwise of the agreeing ones (a bounce or a missed edge among regular periods). Alternating
// periods, ringing crossings, a record regular only in part, noise and a drift beyond 20 % give
// none. Resumable: begin() with the extrema of all points, then feed() every point in
// order (any number of calls), finish() once. The codes are integers, so each threshold is an
// integer comparison with the double threshold rounded once (v <= x iff v <= floor(x), v > x iff
// v > floor(x), v >= x iff v >= ceil(x), v < x iff v < ceil(x)); interpolation stays in double.
//
// Lane timing bursts (burst > 1, records of four capture lanes only): at full rate each lane's DMA
// reads the ADC port when it is served, up to tens of nanoseconds after its timer event, so the
// lanes see a transition a few points apart and one edge reads as a short burst of crossings
// (R,R or F,R,F; build/hardware/scope-measure-ui-review PWM frame of 2026-11-14). Quiet is counted
// in positions seen without a crossing, the same way at both ends and inside. A burst of crossings
// with fewer than `burst` quiet positions between each and the one before, at least `burst` seen
// before it (from the record's first point when it is the first: a burst starting sooner proves
// nothing) and at least `burst` seen after it (fed or in a plateau accounted for; a record ending
// sooner proves nothing), that spans fewer than `burst`
// points and after which the signal stays on the side its first crossing went to (its level then,
// not its last crossing's kind: the hysteresis can pass the middle without one), is that one
// transition: its first crossing counts, the rest do not. A burst after which the signal is back
// where it started (a narrow pulse: R,F), any longer run of crossings (a fast signal) and a burst
// the record cuts short count as before.
class EdgeScanner {
public:
    // burst: the lane timing window in points (laneBurst()); 1: every crossing counts at once.
    void begin(int16_t minimum, int16_t maximum, uint32_t burst = 1);
    // The window of a record: its four-lane layouts' read skew (LaneSkewNs) in points (FourBanks and
    // FourByteBanks at 140 MS/s: 9, InterleavedCh1 at 280 MS/s: 18), else 1.
    static uint32_t laneBurst(const FrameMetadata &metadata);
    static constexpr double LaneSkewNs = 64; // Observed up to ~36 ns (five points at 140 MS/s); margin.
    static constexpr unsigned BurstCapacity = 8;
    // Counts the crossings still held as a possible burst (at the end of a pass, before finish()).
    void close();
    bool active() const { return active_; }
    __attribute__((always_inline)) void feed(int16_t value) {
        if (!active_) return;
        if (!started_) { start(value); return; }
        if (__builtin_expect(held_ != 0, 0) && quietAfter() >= burst_) settle();
        if (value <= rearmFloor_) armed_ = true;
        if (value >= upperCeil_) fallingArmed_ = true;
        if (fallingArmed_ && previous_ >= middleCeil_ && value < middleCeil_) falling(value);
        if (armed_ && previous_ <= middleFloor_ && value > middleFloor_) rising(value);
        previous_ = value;
        ++index_;
    }
    // Every point of a record in order, as feed() over a cursor would take them, with the same
    // result. Where both channels share each word (FrameView::pairs), the record is read in columns
    // through the pair reducer: a column lying wholly below the middle crossing (or wholly above it)
    // can hold no edge after its first point, so that point is fed and the rest only sets the arming
    // its extremes call for; other columns are fed point by point.
    void scan(const FrameView &frame, Channel channel);
    static constexpr uint32_t Column = 64;
    // After a column with a crossing (which scan() then feeds point by point) the next 1, 2, 4, then 8
    // columns are fed point by point without their extremes: a fast signal has crossings in every
    // column. A column accounted for as a whole starts over.
    static constexpr uint32_t MaximumBackoff = 8;
    // scan() in pieces, for any number of channels at once: the points [at, at + n) of each channel
    // whose scanner is given (null: none), n <= Column, following the points each was fed before.
    // One pair-reducer column serves both channels where either takes extremes. Any cut of the
    // record into pieces gives scan()'s result (a cut column is two shorter ones; the back-off
    // counts pieces).
    static void walk(const FrameView &frame, uint32_t at, uint32_t n, EdgeScanner *const (&scanners)[2]);
    // Points scan() accounted for without feeding them one by one.
    uint32_t skipped() const { return skipped_; }
    void finish(double samplesPerSecond, SignalSummary &result) const;
    // The frequency of the bulk of the periods (rising crossing to rising crossing) over the whole
    // record, from one pass: of a uniform sample of PeriodLog periods (a reservoir, deterministic),
    // the median, if at least three quarters of the sample lie within 20 % of it, then the mean of
    // those. A bounce or a missed edge among regular periods passes; noise, alternating periods, or a
    // record regular only in part does not.
    bool votedFrequency(double samplesPerSecond, double &hertz) const;
    static constexpr unsigned PeriodLog = 32;
    static constexpr unsigned ConsensusPrefix = 8; // Periods whose median is finish()'s candidate.
    // A disturbed start (a few short cycles before a clean signal) gives a wrong first candidate:
    // when the consensus on it fails while the uniform sample of all periods has a dominant one,
    // that is the candidate to confirm, in a second pass over the same points after
    // confirm(candidate) (begin() thresholds kept). 0 when the first pass stands or nothing dominates.
    double retryCandidate() const;
    void confirm(double candidate);
    uint32_t edges() const { return edges_; }

private:
    // One piece of walk(): its extremes (null: none, point by point) or the back-off's piece.
    void piece(const FrameView &frame, Channel channel, uint32_t at, uint32_t n, const ColumnStats *stats);
    bool wantsStats() const { return active_ && !direct_; }
    void start(int16_t value);
    void falling(int16_t value);
    void rising(int16_t value);
    // A crossing found at index_: counted at once, held as a possible burst, or counted with the
    // burst it lengthened past one; settle() decides a held burst once burst_ quiet points passed.
    void crossing(bool rising, double position);
    void settle();
    // Points seen after the last crossing (index_ is the next point's index): positions
    // lastCrossing_ + 1 .. index_ - 1, fed or in a plateau accounted for.
    uint32_t quietAfter() const { return index_ - 1 - lastCrossing_; }
    void countHeld(unsigned counted); // The first `counted` held crossings; the rest are dropped.
    void count(bool rising, double position); // Its periods, duty and log.
    struct Crossing { double position; uint32_t at; bool rising; };
    Crossing heldCrossings_[BurstCapacity]{};
    uint32_t burst_ = 1, lastCrossing_ = 0;
    uint8_t held_ = 0;
    bool crossed_ = false;
    double middle_ = 0, first_ = 0, last_ = 0;
    double falling_ = 0, highSamples_ = 0, periodSamples_ = 0;
    void log(double period);
    float periods_[PeriodLog]{};
    uint32_t logged_ = 0, sampler_ = 0x2545F491u;
    // The consensus finish() reports: periods against the candidate, with the high time of those
    // that had a falling crossing inside them (negative: none).
    struct Consensus {
        double candidate = 0, sum = 0, highSum = 0, highPeriods = 0;
        uint32_t agreeing = 0;
        void count(double period, double high);
    };
    void consensus(double period, double high);
    Consensus settled() const; // The consensus as finish() takes it (the prefix's median if under eight periods).
    bool dominant(double &period, double &sum, unsigned &agreeing) const; // The sample's median and its agreeing part.
    Consensus consensus_;
    double prefix_[ConsensusPrefix]{}, prefixHigh_[ConsensusPrefix]{};
    unsigned prefixed_ = 0;
    int rearmFloor_ = 0, upperCeil_ = 0, middleFloor_ = 0, middleCeil_ = 0;
    uint32_t edges_ = 0, index_ = 0, skipped_ = 0;
    uint32_t backoff_ = 0, direct_ = 0; // walk()'s back-off: its length, and pieces still point by point.
    int16_t previous_ = 0;
    bool active_ = false, started_ = false, armed_ = false, fallingArmed_ = false, haveFalling_ = false;
};
// The span of a channel's bulk: its codes without at most Outliers units on each side, a unit being
// a column of Column points of a record or one point of a short series. An isolated spike (one or
// two units) is left out; anything repeating in more units (a pulse train, even a sparse one) stays.
// When that leaves a flat band (under 4 codes) while the extremes are apart, the extremes are the
// signal (rare pulses) and the band is theirs.
struct SignalBand {
    int16_t low = 0, high = 0;
    bool valid = false;
};
class BandEstimate {
public:
    static constexpr unsigned Outliers = 2;
    void add(int16_t low, int16_t high); // One unit's extremes.
    SignalBand band() const;
private:
    int16_t lows_[Outliers + 1]{}, highs_[Outliers + 1]{}; // The lowest minima and highest maxima, sorted.
    unsigned units_ = 0, kept_ = 0;
    int16_t low_ = 0, high_ = 0; // Of every unit.
};
// The edge thresholds: the signal's two state levels (IEEE 181 histogram modes), its plateaus,
// in place of the band's column extremes where those are an edge's overshoot or ringing (a fast
// square: every column holds an edge and its undershoot, so the band's low is the undershoot and
// the 50 % crossing lies where the ringing after it reaches). Top/Bottom/Amplitude keep the band.
// The codes added are a sample of the record or every point of a short series. The record's sample
// is two points of each band column c (SignalBands::Column = 64 points), at offsets (37c) mod 64
// and (37c + 32) mod 64: 37 is odd, so over 64 columns every offset comes once, and a period
// commensurate with the column (64, 32, 16, 64/3...) is still seen at all its phases (a fixed offset
// would see one phase of it and could find a plateau on a slope). It depends on the point indices
// only: the same in every layout and however a job cuts the record. Within the band [L, H] (S = H - L, twice its middle T = L + H),
// the low side is the codes c with 2c < T, the high side those with 2c > T. A side's plateau is the
// window of 2w + 1 codes (w = max(1, S / 16)) holding the most of its codes, clipped to the side,
// the outermost of equal ones; its level is their mean, rounded. A side keeps its band edge unless
// its plateau stands out: at least MinimumCodes codes, a third of the side's, and twice the codes
// within w of the middle (a ramp, a triangle or noise has no plateau that does; a sine's peaks do,
// near the band). A plateau that does moves its side inward to its level widened by its own noise,
// NoiseMargin times the rms of the window's codes (about three standard deviations of a Gaussian
// cut to the window), rounded, never beyond the band: a noisy signal keeps the band's hysteresis,
// an edge's overshoot or ringing past its plateau sets none. The levels stay the band's when they
// would leave under 4 codes.
class StateLevels {
public:
    static constexpr unsigned MinimumCodes = 8;
    static constexpr double NoiseMargin = 4;
    void add(int16_t code) { uint16_t &count = counts_[uint8_t(code + 128)]; if (count != UINT16_MAX) ++count; }
    // The record's sample points of column `column` lying in [from, to) (and in the record).
    void addColumn(const FrameView &frame, Channel channel, uint32_t column, uint32_t from, uint32_t to);
    static constexpr uint32_t Column = 64, Stride = 37;
    SignalBand thresholds(const SignalBand &band) const;
private:
    uint16_t counts_[256]{};
};
class SignalBands {
public:
    static constexpr uint32_t Column = 64;
    static constexpr unsigned Outliers = BandEstimate::Outliers;
    // levels (null: none): each channel's StateLevels sample of the record.
    static bool measure(const FrameView &frame, SignalBand (&bands)[2], StateLevels (*levels)[2] = nullptr);
};
// The frequency of a signal the edges may not resolve (under eight points per cycle): the dominant
// tone of the record, from Goertzel filters (no FFT storage, nothing shared with the spectrum
// view), resumable in bounded steps. Coarse: CoarseSegments segments of Coarse points spread over
// the whole record, their power summed at each bin from about Fs/8 to 15/32 Fs; the strongest
// bin's lobe (three bins) must hold Share of the segments' AC energy (about mean). Then Rounds
// refinements on up to FineSegments segments of 256, 1024, 4096 and 16384 points (fewer and
// shorter in a shorter record), each a parabola through the log power at the estimate and half a
// bin of its length either side (the first one's half bin spans the coarse estimate's error),
// spread over the record as the coarse segments are; the last one's peak must again hold Share of
// its segments' energy. Noise, a ramp, several tones or a drift spread the power and give none;
// a square's odd harmonics leave its fundamental most of it (8/pi^2 without the others' aliases).
// The result lies in (Lowest, Highest] x Fs or is not valid: below Fs/8 the edges measure alone,
// near Nyquist a tone is not resolved (and none above exists in sampled data: an input above Fs/2
// reads as its alias, as on any sampling instrument).
//
// A harmonic folded onto the band can hold as much as the fundamental (40 MHz, duty 1/3, at
// 140 MS/s: its 80 MHz harmonic folds to 60 MHz, 0.45 of CH1's energy against the fundamental's
// 0.49). Only where no single lobe holds Share: the two strongest coarse lobes (at least three bins
// apart) together must hold it (noise ends there); both are refined in the same rounds (six filters,
// HarmonicWeight a point); then one of them, f, must be the fundamental of the other: fold(k x f)
// within CompatibleBins bins of the last round's length of it for a k in 2..Harmonics, while the
// other as a fundamental misses f by more than RivalBins. The coarse bins cannot decide this (the
// rival misses by 7 x the drift from 2/7 Fs, 57 kHz here, under a coarse bin): only the refined
// frequencies do. Either both or neither explaining the other (an exactly commensurate signal,
// 2/7 or 1/7 Fs: a period of 7 points is a 20, 40 or 60 MHz waveform alike; a record too short
// to resolve the drift) gives none. This assumes one periodic signal: two independent tones that
// sit at f and a folded harmonic of it read as that signal's fundamental, which no sampling can
// tell apart.
struct ToneResult {
    double cyclesPerSample = 0; // The tone's frequency over the record's rate.
    float share = 0; // Of the AC energy in the tone, at the last stage reached.
    bool valid = false;
};
class ToneEstimate {
public:
    static constexpr unsigned Coarse = 128, CoarseSegments = 16, CoarseLow = 15, CoarseHigh = 60;
    static constexpr unsigned Rounds = 4, FineSegments = 4, FineFirst = 256;
    // Fewer points: no tone, so nothing above Fs/8 is stated (unchecked edges are not trusted).
    static constexpr unsigned MinimumSamples = 4 * Coarse;
    static constexpr double Share = .6, Lowest = .125, Highest = .45; // Share: two equal tones hold half each.
    // Under 1/Lowest points per cycle the edges stand only when Regular of their periods agree
    // (75 % would let missed cycles through: at 4 to 8 points 20 % is about one point) and their
    // frequency is the tone's within Agreement; otherwise the tone's frequency, or none.
    static constexpr double Regular = .99, Agreement = 5e-4;
    // Record positions a point costs in a step's budget: the coarse stage runs every bin's filter.
    static constexpr uint32_t CoarseWeight = 12, FineWeight = 1, HarmonicWeight = 2;
    static constexpr unsigned Harmonics = 5, CompatibleBins = 2, RivalBins = 4;
    // The most a record's estimate takes (the harmonic path's refinements, reserved by its job).
    static constexpr uint32_t cost(uint32_t samples) {
        uint32_t fine = 0;
        for (uint32_t r = 0, n = FineFirst; r < Rounds; ++r, n *= 4) fine += n < samples ? (samples / n < FineSegments ? samples / n : FineSegments) * n : samples;
        const uint32_t coarse = samples / Coarse < CoarseSegments ? samples / Coarse : CoarseSegments;
        return samples < MinimumSamples ? 0 : CoarseWeight * coarse * Coarse + HarmonicWeight * fine;
    }
    void begin(uint32_t samples, double mean);
    // Advances by whole points within budget (record positions, CoarseWeight or FineWeight each,
    // a remainder under a weight kept for the next step), which it decreases; true once the result
    // is final. cost() is the most it takes: a coarse stage without a tone ends it.
    bool step(const FrameView &frame, Channel channel, uint32_t &budget);
    const ToneResult &result() const { return result_; }
    static ToneResult measure(const FrameView &frame, Channel channel, double mean);
    // The edge result of a summary is kept at eight points per cycle or more (1/Lowest); otherwise
    // (none, or a faster one: a missed or extra cycle moves it) the tone decides, when the edges ran.
    static bool wanted(const SignalSummary &summary, double samplesPerSecond, bool edgesRan);
    // The frequency the summary states: its edges' (regular and the tone's: frequency and pulses,
    // no duty under eight points per cycle), the tone's (source Spectrum, period only: duty and
    // pulses unknown) or none (a faster edge result unconfirmed).
    static void apply(SignalSummary &summary, const ToneResult &tone, double samplesPerSecond);
private:
    static constexpr unsigned Bins = CoarseHigh - CoarseLow + 1;
    void segment(); // The current stage's next segment, or the next stage.
    void coarseDone();
    void roundDone();
    void harmonicDone(); // Of two candidates: the one that is the other's fundamental, or none.
    double candidate(unsigned bin) const; // A coarse lobe's frequency (cycles per sample).
    double lobe(unsigned bin) const;      // Its share of the coarse energy.
    float coefficients_[Bins]{}, s1_[Bins]{}, s2_[Bins]{};
    double power_[Bins]{}, energy_ = 0, mean_ = 0, spacing_ = 0;
    double cycles_[2]{}, shares_[2]{}; // The candidates (cycles per sample) and their last lobe shares.
    uint32_t samples_ = 0, length_ = 0, segments_ = 0, segment_ = 0, at_ = 0, start_ = 0, credit_ = 0;
    uint8_t stage_ = 0, filters_ = 0; // stage_: 0 coarse, 1..Rounds the refinements, Rounds + 1 done.
    uint8_t candidates_ = 1;
    ToneResult result_;
};
class Analyzer {
public:
    // Every point of the record. Extrema, mean and RMS come from RecordMoments (DSP blocks);
    // the edge pass for frequency, period, duty and pulses runs only when edges is set, on the
    // band's StateLevels, then ToneEstimate where the edges do not resolve the signal; the band
    // (SignalBands: Top/Bottom/Amplitude) only when edges or band is.
    static SignalSummary summarize(const FrameView &frame, Channel channel, bool edges = true, bool band = true);
    // The same analysis of a short series already in memory (the screen's column samples), with
    // its own sample rate: samples per second of the series, not of the record.
    static SignalSummary summarize(const int8_t *codes, uint32_t count, double samplesPerSecond, bool edges = true);
};
} // namespace openosc::scope
