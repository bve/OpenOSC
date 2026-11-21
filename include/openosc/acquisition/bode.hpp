#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/acquisition/request.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::scope {
// Frequency response analysis (GitHub #72, ZT703S): the built-in generator's sine swept over a log
// grid, CH1 the reference at the DUT's input, CH2 its output; at each point, once the DUT and the
// capture have settled, both channels' amplitude and phase at the generator's actual frequency (one
// DFT bin under a Hann window, the record's DC taken out), gain = 20 log10 |CH2|/|CH1| in dB and phase
// CH2 - CH1 in degrees, wrapped to (-180, 180]. Volts at the probe tips (each channel's own scale and
// probe), so a probe or range mismatch divides out.
struct BodeSettings {
    static constexpr unsigned MaximumPoints = 256;
    static constexpr uint32_t PointsChoices[] = {5, 10, 20, 50};       // Per decade.
    static constexpr uint32_t SettleChoices[] = {0, 20, 50, 100, 200, 500, 1000, 2000}; // ms
    static constexpr uint32_t AverageChoices[] = {1, 3, 10};          // Records per point.
    uint64_t startMillihertz = 100000, stopMillihertz = 100000000;     // 100 Hz .. 100 kHz.
    uint32_t pointsPerDecade = 10, settleMs = 50;
    uint32_t averages = 1; // Distinct records averaged at each point (1..MaximumAverages).
    static constexpr uint32_t MaximumAverages = 10;
};
// The log grid: count() points (at least 2, at most MaximumPoints) from the start to the stop, both
// clamped to [minimum, maximum] (the DAC's limits at the clock that runs) and the stop above the start
// by at least a ratio; equal steps in log f, pointsPerDecade or fewer when MaximumPoints caps them.
struct BodeGrid {
    uint64_t start = 0, stop = 0; // Millihertz, as clamped.
    unsigned count = 0;
    static BodeGrid of(const BodeSettings &settings, uint64_t minimum, uint64_t maximum);
    uint64_t at(unsigned index) const; // Millihertz, whole.
    bool valid() const { return count >= 2 && start && stop > start; }
};
// One point's measurement and what it says about itself. Invalid points are kept (shown as gaps) with
// the reason; a weak CH2 is a measurement of the gain (deep attenuation), flagged, with no phase.
// ch1Vpp: the stimulus as CH1 measured it (the tone, peak to peak, volts at the tip; 0 when the record
// said nothing); CH2's is ch1Vpp at the gain.
// Averaged (BodeSettings::averages > 1): the mean of the complex CH2/CH1 over the point's valid records
// (the phase its argument, so a phase near +-180 averages without a jump), ch1Vpp their mean; a weak
// CH2 in any of them makes the point weak (no phase) with the mean |CH2/CH1| as its gain. Partial: fewer
// valid records than asked (some invalid, or the point's time ran out), the mean of those there were;
// not invalid. A point with no valid record is its last record's own result.
struct BodePoint {
    enum Flag : uint16_t {
        ClipCh1 = 1, ClipCh2 = 2, WeakCh1 = 4, WeakCh2 = 8, Alias = 16, FewCycles = 32, Incomplete = 64,
        Uncalibrated = 128, NoFrame = 256, NoTone = 512, Partial = 1024,
        Invalid = ClipCh1 | ClipCh2 | WeakCh1 | Alias | FewCycles | Incomplete | Uncalibrated | NoFrame | NoTone,
    };
    float frequencyHz = 0, gainDb = 0, phaseDeg = 0, ch1Vpp = 0;
    uint16_t flags = NoFrame;
    uint8_t range[2]{}; // The V/div indices it was taken at.
    bool valid() const { return !(flags & Invalid); }
    bool phased() const { return valid() && !(flags & WeakCh2); } // A phase to show.
};
static_assert(sizeof(BodePoint) == 20, "A point is 20 bytes (MaximumPoints in ITCM)");
// Both channels' DFT at one known frequency, fed in bounded steps (a record of 32K points per channel
// is not read in one pass). begin() checks what the record can say (rate, cycles, scales); step()
// reads up to `budget` points per channel; result() once done().
class BodeTone {
public:
    static constexpr double MinimumCycles = 3, MaximumFraction = .45; // Of the sample rate: no alias.
    static constexpr int ClipHigh = 125, ClipLow = -126;   // Codes at the ADC's ends: clipped.
    static constexpr double WeakReferenceCodes = 2;        // CH1 amplitude below: no reference.
    static constexpr double WeakOutputCodes = .5;          // CH2 below: flagged (the noise floor).
    // The record (metadata) and the frequency in hertz; `limit` caps the points read (the newest).
    void begin(const FrameMetadata &metadata, double frequencyHz, uint32_t limit = 32768);
    bool step(const FrameView &frame, uint32_t budget);
    // The same accumulation for both channels' codes given directly (tests), in order: `first` is the
    // index within the analysed span where the last call ended.
    void accumulate(uint32_t first, const int16_t *ch1, const int16_t *ch2, uint32_t n);
    bool done() const { return flags_ || at_ >= length_; }
    BodePoint result() const;
    double peakCodes(unsigned channel) const;        // The largest excursion from the mean, in codes.

private:
    struct Sums { double x = 0, xc = 0, xs = 0; int16_t low = 127, high = -128; };
    Sums sums_[2];
    double w_ = 0, wc_ = 0, ws_ = 0; // The window's sums (shared by both channels).
    double omega_ = 0, hann_ = 0, frequency_ = 0;
    double scale_[2]{};              // Volts at the tip per code.
    uint32_t first_ = 0, length_ = 0, at_ = 0;
    uint16_t flags_ = 0;
    template <class Read> void pass(uint32_t first, uint32_t n, Read read); // One pass over n pairs.
};
// What a sweep asks of the instrument (the controller over DataAccess; a fake in the tests).
class BodePort {
public:
    // The generator's sine at `millihertz` (its output on); the frequency it actually plays (Hz).
    virtual bool tune(uint64_t millihertz, double &actualHz) = 0;
    virtual bool configure(const ScopeRequest &request) = 0; // The capture for a point.
    virtual uint64_t nowUs() const = 0;
    virtual bool frame(FrameLease &lease) = 0;               // The newest frame, if any.
protected:
    ~BodePort() = default;
};
// The sweep: one point at a time, every step bounded. A point: tune the generator, configure the
// capture (both channels, Auto on CH1, no Roll, the default 64K record (32768 a channel, all of it read),
// a timebase showing about ten cycles, the current V/div
// adjusted when a channel clips or is too small), wait the settle time, take the first record whose
// first point is after it, measure it in steps; then the next. Results go to the caller's points.
// Averaging: after a record the point stays (no new tune or settle) for the next distinct record (its
// sequence and timestamp not the last one's) until `averages` of them, valid or not, are taken; a range
// change drops what was taken and settles again. Each record has the deadline afresh, so a point ends
// within `averages` deadlines (plus its adjustments); one that runs out keeps what it has (Partial), or
// NoFrame with nothing. The slot of the point being measured holds its last record's result meanwhile.
class BodeSweep {
public:
    enum class State : uint8_t { Idle, Tune, Wait, Measure, Done, Cancelled, Failed };
    enum class Failure : uint8_t { None, Generator, Capture };
    static constexpr unsigned MaximumAdjustments = 4;        // V/div changes for one point.
    static constexpr uint32_t MinimumWaitMs = 3000;           // A record's deadline: at least this,
    static constexpr double WaitRecords = 4;                  // or this many records' durations.
    static constexpr uint32_t StepBudget = 4096;              // Points per channel a step reads.
    // The valid records' sums at the point (CH2/CH1 complex and magnitude, CH1 Vpp; floats: ten at most).
    struct Mean {
        float re = 0, im = 0, magnitude = 0, vpp = 0;
        uint16_t flags = 0; // Theirs, ORed (WeakCh2).
        uint8_t valid = 0;
        void add(const BodePoint &point);
        BodePoint of(const BodePoint &last, unsigned wanted) const; // `last`: the last valid (or any) record.
    };
    bool start(const BodeSettings &settings, const BodeGrid &grid, const ScopeRequest &base, BodePoint *points);
    // One bounded step; true while the sweep runs.
    bool step(BodePort &port);
    void cancel();
    State state() const { return state_; }
    Failure failure() const { return failure_; }
    bool running() const { return state_ == State::Tune || state_ == State::Wait || state_ == State::Measure; }
    unsigned index() const { return index_; }   // The point being measured (or count when done).
    unsigned records() const { return records_; } // Records taken at it since its range was last set.
    unsigned count() const { return grid_.count; }
    const BodeGrid &grid() const { return grid_; }
    static uint64_t timebaseFor(double frequencyHz); // About one cycle per division.

private:
    BodeSettings settings_;
    BodeGrid grid_;
    ScopeRequest request_;
    BodePoint *points_ = nullptr;
    BodeTone tone_;
    FrameLease lease_;
    uint64_t settledUs_ = 0, deadlineUs_ = 0, takenUs_ = 0; // takenUs_/takenSequence_: the last record taken.
    double actualHz_ = 0;
    Mean mean_;
    uint32_t takenSequence_ = 0;
    unsigned index_ = 0, adjustments_ = 0;
    uint8_t records_ = 0;
    bool taken_ = false;
    State state_ = State::Idle;
    Failure failure_ = Failure::None;
    void finishPoint(const BodePoint &point);
    uint64_t recordWaitUs() const; // One record's deadline after the settle (or the last record).
    void fail(Failure failure) { failure_ = failure; state_ = State::Failed; lease_.release(); }
};
// Markers over a finished (or partial) sweep: indices into the points (count when none).
struct BodeMarkers {
    enum Name : uint8_t { None, Peak, Minus3, Phase0, Phase180 }; // Peak first: the -3 dB is under it.
    unsigned peak, minus3, phase0, phase180;
    unsigned total = 0; // The count given to of(): what at() says for none.
    static BodeMarkers of(const BodePoint *points, unsigned count);
    unsigned at(Name name) const { // Its point, or total (None too).
        return name == Peak ? peak : name == Minus3 ? minus3 : name == Phase0 ? phase0 : name == Phase180 ? phase180 : total;
    }
};
// Where the user looks: one selected point shared by the plot and the table (Left/Right one point),
// the named marker the plot's Up/Down went to (None after Left/Right), the form, and the table's pages
// of PageRows (Up/Down a page).
struct BodeCursor {
    static constexpr unsigned PageRows = 9;
    unsigned point = 0;
    BodeMarkers::Name marker = BodeMarkers::None;
    bool table = false;
    void step(int points, unsigned measured);
    void page(int pages, unsigned measured);
    // The next (or previous) named marker in this sweep, wrapping; false (nothing changed) when none is.
    bool jump(bool next, const BodePoint *points, unsigned measured);
    // A sweep's progress: a cursor on the newest point follows the new one; one placed elsewhere stays.
    void follow(unsigned before, unsigned measured);
    unsigned firstRow() const { return point - point % PageRows; } // The table's page.
};
} // namespace openosc::scope
