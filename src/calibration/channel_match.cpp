#include "openosc/calibration/channel_match.hpp"
#include "openosc/calibration/gain_profile.hpp"
#include "openosc/acquisition/ranges.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace openosc::calibration {
namespace {
// The code at which the histogram's cumulative count passes `rank` (bins 0..255 = codes -128..127).
int quantile(const uint16_t (&bins)[256], uint32_t rank) {
    uint32_t seen = 0;
    for (int bin = 0; bin < 256; ++bin) if ((seen += bins[bin]) > rank) return bin - 128;
    return 127;
}
double tipVoltsPerCode(const scope::ChannelMetadata &c) { return c.voltsPerCode * c.probeNumerator / c.probeDenominator; }
} // namespace

void ChannelMatch::begin(unsigned target, const scope::FrameMetadata &setup, double inForce) {
    target_ = target & 1; inForce_ = inForce; passes_ = 0; ratio_[0] = ratio_[1] = 1;
    setup_[0] = setup.channels[0]; setup_[1] = setup.channels[1];
    if (!scope::VoltageRanges::index(setup_[target_].millivoltsPerDivision, range_)) range_ = 0;
    step_ = Step::Ready; issue_ = Issue::None;
}
bool ChannelMatch::sameSetup(const scope::FrameMetadata &metadata) const {
    if (metadata.rolling || metadata.layout == scope::Layout::InterleavedCh1) return false;
    for (unsigned c = 0; c < 2; ++c) {
        const auto &a = setup_[c], &b = metadata.channels[c];
        if (!b.enabled || !b.calibrated || b.coupling != scope::Coupling::Dc || b.millivoltsPerDivision != a.millivoltsPerDivision ||
            b.analogPosition != a.analogPosition ||
            b.probeNumerator != a.probeNumerator || b.probeDenominator != a.probeDenominator || !b.probeDenominator ||
            b.voltsPerCode != a.voltsPerCode || !(b.voltsPerCode > 0) || b.calibrationCrc != a.calibrationCrc)
            return false;
    }
    return setup_[0].millivoltsPerDivision == setup_[1].millivoltsPerDivision;
}
void ChannelMatch::start(uint32_t now, uint64_t nowUs) {
    if (step_ == Step::Measuring || step_ == Step::Review) return;
    if (step_ == Step::Ready) passes_ = 0; // Swap and Failed measure the pass due.
    std::memset(late_, 0, sizeof late_); std::memset(quarterSum_, 0, sizeof quarterSum_); std::memset(quarterCount_, 0, sizeof quarterCount_); std::memset(runs_, 0, sizeof runs_); std::memset(lateCount_, 0, sizeof lateCount_);
    std::memset(earlySum_, 0, sizeof earlySum_); std::memset(earlyCount_, 0, sizeof earlyCount_);
    afterUs_ = nowUs + uint64_t(SettleMs) * 1000; startedAt_ = now; hasSequence_ = false;
    step_ = Step::Measuring; issue_ = Issue::None;
}
bool ChannelMatch::canSwap() const {
    return uint64_t(setup_[0].probeNumerator) * setup_[1].probeDenominator ==
           uint64_t(setup_[1].probeNumerator) * setup_[0].probeDenominator;
}
void ChannelMatch::swap() { if (step_ == Step::Review && passes_ == 1 && canSwap()) step_ = Step::Swap; }
void ChannelMatch::tick(uint32_t now) {
    if (step_ == Step::Measuring && uint32_t(now - startedAt_) >= TimeoutMs) fail(hasSequence_ ? Issue::Square : Issue::NoFrame);
}
void ChannelMatch::take(const scope::FrameView &frame) {
    if (step_ != Step::Measuring || !frame.valid()) return;
    const auto &m = frame.metadata();
    if (!fresh(m) || (hasSequence_ && m.sequence == sequence_)) return;
    if (!sameSetup(m)) { fail(Issue::Setup); return; }
    sequence_ = m.sequence; hasSequence_ = true;
    const uint32_t points = m.samples < FramePoints ? m.samples : FramePoints;
    for (unsigned c = 0; c < 2; ++c) if (!scan(frame, c, points)) return;
    if (complete()) finish();
}
// One record of one channel: its 1 % and 99 % codes set the levels' thresholds (a quarter of the span
// inside each), plateaus are the runs between confirmed crossings; the first and the last are cut by
// the record and left out.
bool ChannelMatch::scan(const scope::FrameView &frame, unsigned channel, uint32_t points) {
    std::memset(scratch_, 0, sizeof scratch_);
    auto cursor = frame.cursor(scope::Channel(channel));
    const uint32_t read = cursor.run(points, [&](int16_t code) { ++scratch_[uint8_t(int8_t(code)) ^ 0x80]; });
    if (read < points) { fail(Issue::Setup); return false; }
    const int low = quantile(scratch_, points / 100), high = quantile(scratch_, points - 1 - points / 100);
    if (low <= -Rail || high >= Rail) { fail(Issue::Saturated); return false; }
    if (high - low < MinimumSpanCodes) { fail(Issue::Span); return false; }
    const int lowAt = low + (high - low) / 4, highAt = high - (high - low) / 4;
    // A level counts from three points in a row: a one-point bus outlier is no crossing.
    unsigned found = 0;
    int state = -1, pending = -1, streak = 0;
    uint32_t first = 0, last = 0, pendingAt = 0;
    bool entered = false; // The open run began at a crossing (not with the record).
    auto c2 = frame.cursor(scope::Channel(channel));
    uint32_t index = 0;
    c2.run(points, [&](int16_t code) {
        const int level = code >= highAt ? 1 : code <= lowAt ? 0 : -1;
        if (level >= 0) {
            if (level == state) { last = index; pending = -1; streak = 0; }
            else if (state < 0) { state = level; first = last = index; }
            else {
                if (level != pending) { pending = level; pendingAt = index; streak = 0; }
                if (++streak == 3) {
                    if (entered && found < MaximumRuns) {
                        accumulate(frame, channel, {first, last, uint8_t(state), int16_t(state ? highAt : low - Window),
                                                    int16_t(state ? high + Window : lowAt)});
                        ++found;
                    }
                    entered = true; state = level; first = pendingAt; last = index; pending = -1; streak = 0;
                }
            }
        }
        ++index;
    });
    return true;
}
// The late half of a plateau into the channel's histogram and, clamped, into the sums of its two
// halves; its first quarter into the early mean.
void ChannelMatch::accumulate(const scope::FrameView &frame, unsigned channel, const Run &run) {
    const uint32_t length = run.last - run.first + 1;
    if (length < RunPoints) return;
    auto early = frame.cursor(scope::Channel(channel), run.first);
    int32_t sum = 0;
    const uint32_t quarter = early.run(length / 4, [&](int16_t code) { sum += code; });
    earlySum_[channel][run.level] += sum; earlyCount_[channel][run.level] += quarter;
    auto &taken = lateCount_[channel][run.level];
    // Keep both portions of the comparison in their own quarter even on the last,
    // partially used plateau. Splitting a truncated prefix could measure the third quarter twice.
    const uint32_t points = std::min((Samples - taken) / 2, length / 4);
    for (unsigned part = 0; part < 2; ++part) {
        auto late = frame.cursor(scope::Channel(channel), run.first + length * (part + 2) / 4);
        int32_t clamped = 0;
        const uint32_t n = late.run(points, [&](int16_t code) {
            ++late_[channel][uint8_t(int8_t(code)) ^ 0x80];
            clamped += std::clamp<int>(code, run.floor, run.ceiling);
        });
        taken += n; quarterSum_[channel][run.level][part] += clamped; quarterCount_[channel][run.level][part] += n;
    }
    ++runs_[channel][run.level];
}
bool ChannelMatch::complete() const {
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned l = 0; l < 2; ++l) if (runs_[c][l] < MinimumRuns || lateCount_[c][l] < Samples) return false;
    return true;
}
// The mean within Window codes of the highest bin in codes [from, to).
ChannelMatch::Level ChannelMatch::level(unsigned channel, int from, int to) const {
    const auto &bins = late_[channel];
    int peak = from;
    for (int code = from; code < to; ++code) if (bins[code + 128] > bins[peak + 128]) peak = code;
    Level out;
    double sum = 0, squares = 0;
    for (int code = peak - Window; code <= peak + Window; ++code) {
        if (code < -128 || code > 127) continue;
        out.inside += bins[code + 128]; sum += double(bins[code + 128]) * code; squares += double(bins[code + 128]) * code * code;
    }
    out.value = out.inside ? sum / out.inside : 0;
    out.rms = out.inside ? std::sqrt(std::max(0.0, squares / out.inside - out.value * out.value)) : 0;
    return out;
}
void ChannelMatch::finish() {
    double tip[2]{}, variance = 0;
    for (unsigned c = 0; c < 2; ++c) {
        const uint32_t total = lateCount_[c][0] + lateCount_[c][1];
        const int middle = (quantile(late_[c], total / 100) + quantile(late_[c], total - 1 - total / 100)) / 2;
        const auto low = level(c, -128, middle), high = level(c, middle, 128);
        if (high.value + Window >= Rail || low.value - Window <= -Rail) { fail(Issue::Saturated); return; }
        span_[c] = high.value - low.value;
        // A plateau still moving in its late half (a probe far off compensation, no square): spread
        // past the windows, or its last half off its first by more than DriftLimit of the span.
        bool moving = total - low.inside - high.inside > total / 64;
        for (unsigned l = 0; l < 2; ++l) {
            const auto &sums = quarterSum_[c][l]; const auto &counts = quarterCount_[c][l];
            moving |= std::abs(double(sums[1]) / counts[1] - double(sums[0]) / counts[0]) > span_[c] * DriftLimit;
        }
        if (moving) { fail(Issue::Flat); return; }
        if (span_[c] < MinimumSpanCodes) { fail(Issue::Span); return; }
        const double earlyHigh = double(earlySum_[c][1]) / earlyCount_[c][1] - high.value,
                     earlyLow = double(earlySum_[c][0]) / earlyCount_[c][0] - low.value;
        compensation_[c] = (earlyHigh - earlyLow) / 2 / span_[c];
        tip[c] = span_[c] * tipVoltsPerCode(setup_[c]);
        for (const auto &l : {low, high}) if (l.rms < .5) variance += (.29 / span_[c]) * (.29 / span_[c]);
    }
    ratio_[passes_] = tip[reference()] / tip[target_];
    resolution_ = std::sqrt(variance);
    ++passes_;
    if (!GainProfile::allowed(proposed())) { --passes_; fail(Issue::Implausible); return; }
    step_ = Step::Review;
}
double ChannelMatch::ratio() const {
    return passes_ >= 2 ? std::sqrt(ratio_[0] * ratio_[1]) : passes_ ? ratio_[0] : 1;
}
double ChannelMatch::probeDifference() const { return passes_ >= 2 ? std::sqrt(ratio_[0] / ratio_[1]) - 1 : 0; }
unsigned ChannelMatch::percent() const {
    if (step_ != Step::Measuring) return step_ == Step::Review ? 100 : 0;
    uint32_t least = Samples;
    for (unsigned c = 0; c < 2; ++c) for (unsigned l = 0; l < 2; ++l) if (lateCount_[c][l] < least) least = lateCount_[c][l];
    return unsigned(uint64_t(least) * 99 / Samples);
}
} // namespace openosc::calibration
