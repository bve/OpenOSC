#pragma once
#include "openosc/data/status.hpp"
#include "openosc/acquisition/adc_bus_glitch.hpp"
#include "openosc/acquisition/interleave_match.hpp"
#include <cstddef>

namespace openosc::scope {
enum class Channel : uint8_t { Ch1, Ch2 };
// FourBanks: lane i of the capture holds points 4n+i as 16-bit words, CH1 low byte, CH2 high.
// FourByteBanks: the same lanes as byte arrays of the one enabled channel (8-bit capture).
// InterleavedCh1 (ZT706S; ZT703S with Interleave CH1 on): CH1 alone through both ADCs clocked in opposite phase; word w (in the
// FourBanks lane order) holds points 2w and 2w+1. The factory spline (0804E640) takes point 2w
// from the HIGH byte, converted first, and adds the calibration correction to it; 2w+1 is the
// LOW byte as it is.
enum class Layout : uint8_t { Linear, FourBanks, InterleavedCh1, FourByteBanks };
// Points per 16-bit word of a layout's banks (InterleavedCh1: two of one channel).
constexpr unsigned pointsPerWord(Layout layout) { return layout == Layout::InterleavedCh1 ? 2 : 1; }
// The correction of the HIGH ADC's points of an InterleavedCh1 record, in codes: the factory float
// (calibration +154) or the user's service value, frozen in the record's metadata. Applied to the
// signed code as one whole code (round half away from zero) and kept within -128..127; never stored
// in the captured words, so a record is corrected exactly once wherever it is read.
struct InterleaveCorrection {
    static constexpr float Limit = 16; // Codes; the service adjustment stays within it.
    static bool valid(float offset) { return offset == offset && offset >= -Limit && offset <= Limit; }
    static int8_t shift(float offset) {
        return int8_t(offset < 0 ? -int(-offset + 0.5f) : int(offset + 0.5f));
    }
    __attribute__((always_inline)) static uint8_t apply(uint8_t raw, int shift) {
        int code = int(int8_t(raw)) + shift;
        code = code < -128 ? -128 : code > 127 ? 127 : code;
        return uint8_t(int8_t(code));
    }
};
enum class Coupling : uint8_t { Dc, Ac };
struct ChannelMetadata {
    uint32_t millivoltsPerDivision = 500, probeNumerator = 1, probeDenominator = 1;
    double voltsPerCode = 0, zeroCode = 0;
    uint16_t calibrationCrc = 0;
    Coupling coupling = Coupling::Dc;
    int16_t analogPosition = 80; // Screen line 0..160 the analog zero was set to for this record.
    bool enabled = true, calibrated = false;
};
// The edge a record's triggerIndex was found for: its source, slope and level code (GitHub #34: a view
// places the crossing between triggerIndex - 1 and triggerIndex on the curve it draws, TriggerPhase).
// Display only: not in any file format, SCPI reply or the record's samples.
struct TriggerEdge {
    int16_t level = 0;
    Channel channel = Channel::Ch1;
    bool rising = true, known = false;
};
struct FrameMetadata {
    static constexpr uint32_t MaximumRing = 524288; // Points a capture, Roll or imported ring may hold.
    uint64_t timestampUs = 0;
    uint64_t timebaseNs = 0; // Display division; zero means fit the complete legacy record.
    Rational samplesPerSecond;
    uint32_t sequence = 0, samples = 0, ring = 0, origin = 0, triggerIndex = 0;
    Layout layout = Layout::FourBanks;
    bool timingVerified = false, triggered = false, rolling=false;
    // A capture the trigger was forced on (TFOR): placed at the force point unless `triggered` (an
    // edge the settings ask for is in the data). Not part of any record or file format.
    bool forced = false;
    // A triggered record whose edge is not at its configured pretrigger point (TriggerRecord::select:
    // no edge the ring holds the whole record around): triggerIndex minus that point. The view places
    // the edge where the pretrigger point is. Zero for every other record; not in any file format.
    int32_t triggerShift = 0;
    int32_t eventOffset = 0; bool eventKnown = false; // The comparator event, in points from triggerIndex (WAV:EVENt?).
    TriggerEdge triggerEdge; // What triggerIndex was found for (known only while triggered).
    // InterleavedCh1 only: the HIGH ADC's correction this record is read with (InterleaveCorrection).
    float interleaveOffset = 0;
    // Immutable boot profile entry. Transferred file payloads already hold the corrected codes.
    const InterleaveMatch *interleaveMatch = nullptr;
    ChannelMetadata channels[2];
    bool valid() const;
    // No trigger: not triggered, index and shift zero (the one reset of all three).
    void clearTrigger() { triggered = false; triggerIndex = 0; triggerShift = 0; eventOffset = 0; eventKnown = false; triggerEdge = {}; }
};
// A FourBanks or InterleavedCh1 record in two parts (CaptureBuffer::split): each lane keeps its newest
// ahbWords in the capture ring (bank i of the view, a power of two) and its older words in a history
// the MDMA copied them to. The view's lane ring (FrameView::laneRing: ring / 4 words, ring / 8 for
// InterleavedCh1, two points a word) is ahbWords + historyWords; chronological lane
// word t lies at ring offset t mod ring/4, in the capture ring at t mod ahbWords while it is one of
// the newest ahbWords, else in the history at t mod historyWords. Frozen with the record.
struct SplitHistory {
    const uint16_t *older[4]{};
    uint32_t ahbWords = 0, historyWords = 0;
    uint32_t end[4]{};    // Lane ring offset just past the newest word.
    uint32_t newest[4]{}; // History index of the newest word (where it would be copied to).
    bool active() const { return historyWords != 0; }
    uint16_t word(const uint16_t *const *ahb, unsigned lane, uint32_t offset, uint32_t laneRing) const {
        uint32_t age = end[lane] + laneRing - 1 - offset; // 0 is the newest word; offset < laneRing.
        if (age >= laneRing) age -= laneRing;
        if (age < ahbWords) return ahb[lane][offset & (ahbWords - 1)];
        uint32_t index = newest[lane] + 2 * historyWords - age; // age < laneRing <= 2 * historyWords.
        while (index >= historyWords) index -= historyWords;
        return older[lane][index];
    }
};
class FrameView {
public:
    class Cursor;
    class PairCursor;
    FrameView() = default;
    // wordsPerBank is each bank's capacity in 16-bit words, whatever the layout stores in it.
    FrameView(const FrameMetadata &metadata, const uint16_t *const *banks, size_t wordsPerBank,
              const SplitHistory *split = nullptr);
    bool valid() const { return valid_; }
    bool code(Channel channel, size_t index, uint8_t &output) const;
    bool signedCode(Channel channel, size_t index, int16_t &output) const;
    // Display interpolation on paired ADCs retains the fractional part of their offset.
    bool interleavedQ8(uint32_t index, int32_t &output) const;
    // Count genuine rails before the offset moves a captured code away from the limit.
    uint32_t interleaveRails(uint32_t first, uint32_t count, uint32_t limit) const;
    bool volts(Channel channel, size_t index, double &output) const;
    const FrameMetadata &metadata() const { return metadata_; }
    // The trigger at `index` (none without `found`): a record annotated again with its own trigger
    // keeps its triggerShift, any other trigger has none.
    // `edge`: what the index was found for; the record's own is kept when it already holds this index.
    FrameView withTrigger(bool found, uint32_t index, TriggerEdge edge = {}) const;
    // The trigger at `index`, `shift` points past the record's pretrigger point (FrameMetadata::triggerShift).
    FrameView withShiftedTrigger(uint32_t index, int32_t shift, TriggerEdge edge = {}) const;
    FrameView withEvent(int32_t offset) const { FrameView view = *this; view.metadata_.eventOffset = offset; view.metadata_.eventKnown = true; return view; }
    // Calibration compares the same captured words with and without new coefficients.
    FrameView withInterleaveMatch(const InterleaveMatch *match) const;
    FrameView slice(uint32_t first,uint32_t count) const;
    const uint16_t *bank(unsigned index) const { return index < 4 ? banks_[index] : nullptr; }
    const SplitHistory &split() const { return split_; }
    Cursor cursor(Channel channel, uint32_t first = 0) const &;
    Cursor cursor(Channel channel, uint32_t first = 0) const && = delete;
    // Both channels share each 16-bit word (CH1 low byte, CH2 high byte): Linear and FourBanks
    // records, split or not, with both channels enabled. PairCursor then reads every word once.
    bool pairs() const {
        return valid_ && metadata_.channels[0].enabled && metadata_.channels[1].enabled &&
               (metadata_.layout == Layout::Linear || metadata_.layout == Layout::FourBanks);
    }
    PairCursor pairCursor(uint32_t first = 0) const &;
    PairCursor pairCursor(uint32_t first = 0) const && = delete;

private:
    FrameMetadata metadata_{};
    const uint16_t *banks_[4]{};
    SplitHistory split_{};
    size_t wordsPerBank_ = 0;
    int32_t matchBiasQ23_ = 0;
    int16_t evenShiftQ8_ = 0;
    int8_t evenShift_ = 0; // InterleaveCorrection::shift of the metadata's offset.
    bool valid_ = false;
    bool validate() const;
    // The corrected code (no bus correction) at a record index; the neighbours of a suspect point.
    uint8_t byteAt(unsigned channel, size_t index) const;
    // The byte as captured at a ring position (InterleavedCh1: even HIGH, odd LOW of word position/2).
    __attribute__((always_inline)) uint8_t byteAtPosition(unsigned channel, uint32_t position) const {
        if (metadata_.layout == Layout::FourByteBanks)
            return reinterpret_cast<const uint8_t *>(banks_[position & 3])[position >> 2];
        if (metadata_.layout == Layout::InterleavedCh1) {
            const uint16_t word = interleaveWordAt(position >> 1);
            return position & 1 ? uint8_t(word) : uint8_t(word >> 8);
        }
        return uint8_t(wordAt(position) >> (channel * 8));
    }
    // A captured byte as the record is read: the HIGH ADC's correction on InterleavedCh1's even points.
    __attribute__((always_inline)) uint8_t corrected(uint8_t raw, uint32_t position) const {
        return evenShift_ && !(position & 1) ? InterleaveCorrection::apply(raw, evenShift_) : raw;
    }
    // Words per lane of the view's ring (SplitHistory::word): every layout's lanes hold ring/4 points.
    uint32_t laneRing() const { return metadata_.ring / (4 * pointsPerWord(metadata_.layout)); }
    // The 16-bit word at a word position of the Linear, FourBanks or InterleavedCh1 banks.
    __attribute__((always_inline)) uint16_t wordAt(uint32_t position) const {
        const unsigned bank = metadata_.layout == Layout::Linear ? 0 : position & 3;
        const uint32_t offset = metadata_.layout == Layout::Linear ? position : position >> 2;
        return split_.historyWords ? split_.word(banks_, bank, offset, laneRing()) : banks_[bank][offset];
    }
    __attribute__((always_inline)) uint16_t interleaveWordAt(uint32_t position) const {
        const unsigned bank = position & 3;
        const uint32_t offset = position >> 2;
        return split_.historyWords ? split_.word(banks_, bank, offset, metadata_.ring >> 3) : banks_[bank][offset];
    }
    // A suspect captured byte at a record index: resolved from its corrected neighbours, else the
    // byte itself, corrected.
    uint8_t resolveGlitch(unsigned channel, size_t index, uint8_t raw) const;
    bool matchSuspect(uint8_t raw, uint32_t index, uint32_t position) const;
    bool matchInterleaveQ8(uint32_t index, int32_t &output) const;
    int16_t matchedCode(uint32_t index, uint32_t position) const;
};
// Sequential analysis validates its immutable frame/channel once and advances the ring without
// a division or repeated channel checks per point. Random access and this cursor share the same
// physical mapping and ADC correction. The uint32 wrap before ring reduction is preserved.
// One display column of one channel, in record order: its first and last codes, extremes with the
// index (from the column's start) where each first occurs, sum and count.
struct ColumnStats {
    int16_t first = 0, last = 0, low = 0, high = 0;
    int32_t sum = 0; // 524288 codes of 128 fit.
    uint32_t lowAt = 0, highAt = 0, count = 0;
};
class FrameView::Cursor {
    friend class FrameView;
    Cursor(const FrameView &frame, Channel channel, uint32_t first = 0)
        : frame_(frame), channel_(unsigned(channel)), index_(first), logical_(uint32_t(frame.metadata_.origin + first)) {
        if (frame.valid_ && channel_ < 2 && frame.metadata_.channels[channel_].enabled &&
            !(frame.metadata_.layout == Layout::InterleavedCh1 && channel_) && first < frame.metadata_.samples) {
            remaining_ = frame.metadata_.samples - first;
            position_ = logical_ % frame.metadata_.ring;
        }
    }
public:
    __attribute__((always_inline)) bool next(int16_t &output) {
        if (!remaining_) return false;
        if (__builtin_expect(frame_.metadata_.interleaveMatch && !(position_ & 1), 0))
            output = frame_.matchedCode(index_, position_);
        else {
            const uint8_t raw = frame_.byteAtPosition(channel_, position_);
            const uint8_t code = __builtin_expect(AdcBusGlitch::suspect(raw), 0) ? frame_.resolveGlitch(channel_, index_, raw)
                                                                                  : frame_.corrected(raw, position_);
            output = code < 128 ? code : int16_t(code) - 256;
        }
        --remaining_; ++index_;
        if (++logical_ == 0 || ++position_ == frame_.metadata_.ring) position_ = 0;
        return true;
    }
    // The next n points in order, f(code) for each (fewer once the record ends; returns how many):
    // the same codes, order and bus correction as n calls of next(), with the layout resolved once
    // for the run instead of for every point.
    template <class F> __attribute__((always_inline)) uint32_t run(uint32_t n, F &&f) {
        if (n > remaining_) n = remaining_;
        const auto &m = frame_.metadata_;
        if (frame_.split_.historyWords || m.interleaveMatch) {
            int16_t code = 0;
            for (uint32_t k = 0; k < n; ++k) { next(code); f(code); }
            return n;
        }
        const unsigned shift = 8 * channel_;
        if (m.layout == Layout::InterleavedCh1) {
            const int shiftEven = frame_.evenShift_;
            loop(n, f, [this](uint32_t p) __attribute__((always_inline)) {
                const uint32_t w = p >> 1;
                const uint16_t word = frame_.banks_[w & 3][w >> 2];
                return p & 1 ? uint8_t(word) : uint8_t(word >> 8);
            }, [shiftEven](uint8_t raw, uint32_t p) __attribute__((always_inline)) {
                return shiftEven && !(p & 1) ? InterleaveCorrection::apply(raw, shiftEven) : raw;
            });
        } else if (m.layout == Layout::FourByteBanks)
            loop(n, f, [this](uint32_t p) __attribute__((always_inline)) { return reinterpret_cast<const uint8_t *>(frame_.banks_[p & 3])[p >> 2]; });
        else if (m.layout == Layout::FourBanks)
            loop(n, f, [this, shift](uint32_t p) __attribute__((always_inline)) { return uint8_t(frame_.banks_[p & 3][p >> 2] >> shift); });
        else
            loop(n, f, [this, shift](uint32_t p) __attribute__((always_inline)) { return uint8_t(frame_.banks_[0][p] >> shift); });
        return n;
    }
    // The next n points reduced into one column (the projection's inner loop): out of line so the
    // reduction keeps its state in registers. Returns the points read.
    uint32_t column(uint32_t n, ColumnStats &out);
private:
    struct AsCaptured {
        __attribute__((always_inline)) uint8_t operator()(uint8_t raw, uint32_t) const { return raw; }
    };
    // read(position): the captured byte; fix(raw, position): the record's correction of it.
    template <class F, class Read, class Fix = AsCaptured>
    __attribute__((always_inline)) void loop(uint32_t n, F &f, Read read, Fix fix = {}) {
        const uint32_t ring = frame_.metadata_.ring;
        uint32_t index = index_, logical = logical_, position = position_; // Registers for the run.
        for (uint32_t k = 0; k < n; ++k) {
            const uint8_t raw = read(position);
            const uint8_t code = __builtin_expect(AdcBusGlitch::suspect(raw), 0) ? frame_.resolveGlitch(channel_, index, raw)
                                                                                  : fix(raw, position);
            f(int16_t(code < 128 ? code : int16_t(code) - 256));
            ++index;
            if (++logical == 0 || ++position == ring) position = 0;
        }
        index_ = index; logical_ = logical; position_ = position;
        remaining_ -= n;
    }
    const FrameView &frame_;
    unsigned channel_;
    uint32_t index_, logical_, position_ = 0, remaining_ = 0;
};
// Both channels of a pairs() record in one pass: one address resolution and one word read per
// point for the two ColumnStats. Codes, order, bus correction and the uint32 origin wrap are those
// of each channel's Cursor, which stays the reference (tests compare every field).
class FrameView::PairCursor {
    friend class FrameView;
    PairCursor(const FrameView &frame, uint32_t first)
        : frame_(frame), index_(first), logical_(uint32_t(frame.metadata_.origin + first)) {
        if (frame.pairs() && first < frame.metadata_.samples) {
            remaining_ = frame.metadata_.samples - first;
            position_ = logical_ % frame.metadata_.ring;
        }
    }
public:
    // The next n points (fewer once the record ends) into out[0] (CH1) and out[1] (CH2).
    uint32_t column(uint32_t n, ColumnStats (&out)[2]);
    // The next point's two codes (false once the record ends): the same word, bus correction and
    // order as column() and as each channel's Cursor (the decoder's CANH - CANL).
    bool next(int16_t &ch1, int16_t &ch2);
private:
    template <class Read> uint32_t reduce(uint32_t n, ColumnStats (&out)[2], Read read);
    uint32_t corrected(uint16_t word, uint32_t index) const; // A suspect point's codes, packed.
    const FrameView &frame_;
    uint32_t index_, logical_, position_ = 0, remaining_ = 0;
};
inline FrameView::Cursor FrameView::cursor(Channel channel, uint32_t first) const & { return {*this, channel, first}; }
inline FrameView::PairCursor FrameView::pairCursor(uint32_t first) const & { return {*this, first}; }
} // namespace openosc::scope
