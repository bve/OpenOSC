#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/record_moments.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
bool TriggerSettings::valid() const {
    return unsigned(channel) < 2 && unsigned(edge) < 2 && unsigned(mode) < 3 && level >= -128 && level <= 127 && position<=1000 &&
           unsigned(type) < 4 && unsigned(condition) < 5 && level2 >= -128 && level2 <= 127 &&
           minimumNs >= MinimumNs && minimumNs <= MaximumNs && maximumNs >= MinimumNs && maximumNs <= MaximumNs;
}
TriggerEdge TriggerSettings::tag() const {
    return {comparatorLevel(), channel, comparatorEdge() == Edge::Rising, true};
}
bool TriggerSettings::comparatorUsesLevel2() const {
    if (!twoLevels()) return false;
    const bool upper = (edge == Edge::Rising) == (type == TriggerType::Transition);
    return upper ? level2 > level : level2 <= level;
}
void TriggerSettings::setComparatorLevel(int16_t code) {
    if (!twoLevels()) { level = code; return; }
    const bool upper = (edge == Edge::Rising) == (type == TriggerType::Transition);
    const bool secondary = comparatorUsesLevel2();
    const auto other = secondary ? level : level2;
    (secondary ? level2 : level) = upper ? std::max(code, other) : std::min(code, other);
}
int TriggerSettings::armLevel() const {
    if (!reject) return level;
    return edge == Edge::Rising ? std::max(-128, level - RejectCodes) : std::min(127, level + RejectCodes);
}
namespace {
// Extrema, mean and RMS from the moments of all count points; valid when they hold them all.
bool fill(SignalSummary &result, const dsp::Moments &m, uint32_t count) {
    if (!count || m.count != count) return false;
    result.minimum = m.minimum; result.maximum = m.maximum;
    result.mean = double(m.sum) / count;
    result.rms = std::sqrt(double(m.squares) / count);
    return result.valid = true;
}
void robust(SignalSummary &result, const SignalBand &band) {
    result.hasRobust = band.valid;
    if (band.valid) { result.robustMinimum = band.low; result.robustMaximum = band.high; }
}
} // namespace
uint32_t EdgeScanner::laneBurst(const FrameMetadata &metadata) {
    // The four DMA lanes of every capture layout; InterleavedCh1's words (two points each) are read by
    // the same lanes, so the same skew in time is twice the points at its rate.
    if (metadata.layout != Layout::FourBanks && metadata.layout != Layout::FourByteBanks &&
        metadata.layout != Layout::InterleavedCh1) return 1;
    const double points = std::ceil(LaneSkewNs * 1e-9 * metadata.samplesPerSecond.value());
    return points > 1 ? uint32_t(std::min(points, 1e6)) : 1;
}
void EdgeScanner::begin(int16_t minimum, int16_t maximum, uint32_t burst) {
    *this = {};
    burst_ = burst ? burst : 1;
    if (maximum - minimum < 4) return;
    middle_ = (maximum + minimum) * .5;
    const double rearm = minimum + (maximum - minimum) * .3;
    const double upper = minimum + (maximum - minimum) * .7;
    rearmFloor_ = int(std::floor(rearm)); upperCeil_ = int(std::ceil(upper));
    middleFloor_ = int(std::floor(middle_)); middleCeil_ = int(std::ceil(middle_));
    active_ = true;
}
void EdgeScanner::start(int16_t value) {
    started_ = true;
    armed_ = value <= rearmFloor_;
    fallingArmed_ = value >= upperCeil_;
    previous_ = value;
    index_ = 1;
}
void EdgeScanner::falling(int16_t value) {
    fallingArmed_ = false;
    crossing(false, index_ - 1 + (previous_ - middle_) / (previous_ - value));
}
void EdgeScanner::rising(int16_t value) {
    armed_ = false;
    crossing(true, index_ - 1 + (middle_ - previous_) / (value - previous_));
}
void EdgeScanner::crossing(bool rising, double position) {
    const uint32_t at = index_;
    if (burst_ <= 1) { count(rising, position); return; }
    // Held: a crossing comes before burst_ quiet positions passed (feed() settles otherwise). The
    // first of a burst needs burst_ quiet positions seen before it: since the last crossing, or
    // positions 0 .. at - 1 for the record's first.
    const bool chained = held_ ? at - heldCrossings_[0].at >= burst_ || held_ == BurstCapacity
                               : (crossed_ ? at - 1 - lastCrossing_ : at) < burst_;
    crossed_ = true; lastCrossing_ = at;
    if (chained) { // Not an isolated short burst: everything counts, in order.
        for (unsigned i = 0; i < held_; ++i) count(heldCrossings_[i].rising, heldCrossings_[i].position);
        held_ = 0;
        count(rising, position);
        return;
    }
    heldCrossings_[held_++] = {position, at, rising};
}
void EdgeScanner::settle() {
    // After the quiet the signal is on the side the first crossing went to: one transition, at its
    // first crossing. The side is the level itself (previous_, the last point seen), not the last
    // crossing's kind: hysteresis can let the last pass of the middle go without a crossing.
    const bool high = previous_ > middleFloor_;
    countHeld(held_ > 1 && heldCrossings_[0].rising == high ? 1 : held_);
}
void EdgeScanner::countHeld(unsigned counted) {
    for (unsigned i = 0; i < counted; ++i) count(heldCrossings_[i].rising, heldCrossings_[i].position);
    held_ = 0;
}
void EdgeScanner::close() {
    if (!held_) return;
    // The quiet after a burst must have been seen (fed or a plateau accounted for): a record that
    // ends sooner leaves it unproven, and every crossing it held counts.
    if (quietAfter() >= burst_) settle();
    else countHeld(held_);
}
void EdgeScanner::count(bool rising, double position) {
    if (!rising) {
        falling_ = position;
        haveFalling_ = edges_ && falling_ > last_;
        return;
    }
    if (!edges_)
        first_ = position;
    else {
        const double period = position - last_;
        const bool high = haveFalling_ && falling_ < position;
        if (high) {
            highSamples_ += falling_ - last_;
            periodSamples_ += period;
        }
        log(period);
        consensus(period, high ? falling_ - last_ : -1);
    }
    last_ = position;
    haveFalling_ = false;
    ++edges_;
}
void EdgeScanner::log(double period) {
    // A uniform sample of all periods (reservoir, algorithm R) with a fixed linear congruential sequence.
    const uint32_t index = edges_ - 1; // Of this period among all, from 0.
    if (index < PeriodLog) { periods_[logged_++] = float(period); return; }
    sampler_ = sampler_ * 1664525u + 1013904223u;
    const uint32_t slot = uint32_t(uint64_t(sampler_) * (index + 1) >> 32);
    if (slot < PeriodLog) periods_[slot] = float(period);
}
namespace {
double medianOf(const double *values, unsigned count) {
    double sorted[EdgeScanner::ConsensusPrefix];
    std::copy(values, values + count, sorted);
    std::nth_element(sorted, sorted + count / 2, sorted + count);
    return sorted[count / 2];
}
} // namespace
void EdgeScanner::Consensus::count(double period, double high) {
    if (std::abs(period - candidate) > .2 * candidate) return;
    ++agreeing; sum += period;
    if (high >= 0) { highSum += high; highPeriods += period; }
}
// The first ConsensusPrefix periods wait for the candidate (their median), then every period is
// counted against it as it comes: one pass, no store of the record's periods.
void EdgeScanner::consensus(double period, double high) {
    if (prefixed_ < ConsensusPrefix) {
        prefix_[prefixed_] = period; prefixHigh_[prefixed_] = high;
        if (++prefixed_ < ConsensusPrefix) return;
        consensus_.candidate = medianOf(prefix_, prefixed_);
        for (unsigned i = 0; i < prefixed_; ++i) consensus_.count(prefix_[i], prefixHigh_[i]);
        return;
    }
    consensus_.count(period, high);
}
EdgeScanner::Consensus EdgeScanner::settled() const {
    Consensus c = consensus_;
    if (prefixed_ < ConsensusPrefix && prefixed_) { // Fewer periods than the prefix: their median, now.
        c.candidate = medianOf(prefix_, prefixed_);
        for (unsigned i = 0; i < prefixed_; ++i) c.count(prefix_[i], prefixHigh_[i]);
    }
    return c;
}
double EdgeScanner::retryCandidate() const {
    const uint32_t periods = edges_ ? edges_ - 1 : 0;
    if (!active_ || !periods || 4 * settled().agreeing >= 3 * periods) return 0;
    double period = 0, sum = 0;
    unsigned agreeing = 0;
    return dominant(period, sum, agreeing) ? period : 0;
}
void EdgeScanner::confirm(double candidate) {
    const int rearm = rearmFloor_, upper = upperCeil_, middleFloor = middleFloor_, middleCeil = middleCeil_;
    const double middle = middle_;
    const uint32_t burst = burst_;
    *this = {};
    rearmFloor_ = rearm; upperCeil_ = upper; middleFloor_ = middleFloor; middleCeil_ = middleCeil; middle_ = middle;
    burst_ = burst;
    active_ = true;
    consensus_.candidate = candidate; prefixed_ = ConsensusPrefix; // Every period is counted against it.
}
void EdgeScanner::finish(double samplesPerSecond, SignalSummary &result) const {
    if (!active_) return;
    result.pulses = edges_;
    const uint32_t periods = edges_ ? edges_ - 1 : 0;
    if (!periods || last_ <= first_) return;
    const Consensus c = settled();
    if (4 * c.agreeing < 3 * periods) return;
    if (c.agreeing == periods) { // Every period: the whole span and all high times, as before.
        result.frequencyHz = periods * samplesPerSecond / (last_ - first_);
        if (periodSamples_ > 0) result.dutyPercent = 100 * highSamples_ / periodSamples_;
    } else {
        result.frequencyHz = c.agreeing * samplesPerSecond / c.sum;
        if (c.highPeriods > 0) result.dutyPercent = 100 * c.highSum / c.highPeriods;
    }
    result.hasFrequency = std::isfinite(result.frequencyHz) && result.frequencyHz > 0;
    result.hasDuty = result.hasFrequency && (c.agreeing == periods ? periodSamples_ > 0 : c.highPeriods > 0) &&
                     std::isfinite(result.dutyPercent);
    if (result.hasFrequency) {
        result.frequencySource = FrequencySource::Edges; result.frequencySpan = last_ - first_;
        result.frequencyAgreement = double(c.agreeing) / periods;
    }
    if (!result.hasFrequency) result.frequencyHz = 0;
    if (!result.hasDuty) result.dutyPercent = 0;
}
void EdgeScanner::scan(const FrameView &frame, Channel channel) {
    if (!active_) return;
    const uint32_t samples = frame.metadata().samples;
    EdgeScanner *scanners[2] = {};
    scanners[unsigned(channel) & 1] = this;
    for (uint32_t at = 0; at < samples;) {
        const uint32_t n = std::min(Column, samples - at);
        walk(frame, at, n, scanners);
        at += n;
    }
    close();
}
void EdgeScanner::walk(const FrameView &frame, uint32_t at, uint32_t n, EdgeScanner *const (&scanners)[2]) {
    ColumnStats stats[2];
    const bool pairs = frame.pairs();
    // One channel per word: the reducer would read point by point anyway.
    const bool reduce = pairs && ((scanners[0] && scanners[0]->wantsStats()) || (scanners[1] && scanners[1]->wantsStats()));
    if (reduce && frame.pairCursor(at).column(n, stats) != n) return;
    for (unsigned c = 0; c < 2; ++c)
        if (scanners[c] && scanners[c]->active_) scanners[c]->piece(frame, Channel(c), at, n, reduce ? &stats[c] : nullptr);
}
void EdgeScanner::piece(const FrameView &frame, Channel channel, uint32_t at, uint32_t n, const ColumnStats *stats) {
    const auto pointwise = [&] {
        frame.cursor(channel, at).run(n, [this](int16_t code) __attribute__((always_inline)) { feed(code); });
    };
    if (direct_) { --direct_; pointwise(); return; } // Pieces after a crossing: no extremes, point by point.
    if (!stats) { pointwise(); return; }
    const auto &s = *stats;
    // Below: every code under the middle crossing, so no rising edge (each is <= middleFloor)
    // and no falling one after the first point (each previous is under middleCeil). Above:
    // every code over middleFloor, so no falling edge (each is >= middleCeil) and no rising
    // one after the first. Only arming changes there: armed by a code at or under rearmFloor,
    // which only a piece below holds; falling-armed by one at or over upperCeil, only above.
    const bool below = s.high < middleCeil_, above = s.low > middleFloor_;
    if (started_ && (below || above)) {
        feed(s.first);
        if (below && s.low <= rearmFloor_) armed_ = true;
        if (above && s.high >= upperCeil_) fallingArmed_ = true;
        previous_ = s.last;
        index_ += n - 1;
        skipped_ += n - 1;
        backoff_ = 0;
    } else {
        pointwise();
        backoff_ = backoff_ ? std::min(2 * backoff_, MaximumBackoff) : 1;
        direct_ = backoff_;
    }
}
bool EdgeScanner::dominant(double &period, double &sum, unsigned &agreeing) const {
    const unsigned count = logged_;
    if (count < 2) return false;
    float sorted[PeriodLog];
    std::copy(periods_, periods_ + count, sorted);
    std::nth_element(sorted, sorted + count / 2, sorted + count);
    const double median = sorted[count / 2];
    sum = 0; agreeing = 0;
    for (unsigned i = 0; i < count; ++i)
        if (std::abs(periods_[i] - median) <= median * .2) { sum += periods_[i]; ++agreeing; }
    period = median;
    return median > 0 && 4 * agreeing >= 3 * count;
}
bool EdgeScanner::votedFrequency(double samplesPerSecond, double &hertz) const {
    double median = 0, sum = 0;
    unsigned inliers = 0;
    if (!dominant(median, sum, inliers)) return false;
    hertz = samplesPerSecond * inliers / sum;
    return std::isfinite(hertz) && hertz > 0;
}
namespace {
// The band and edge passes and the tone estimate in frames of their own, one after the other: inlined
// together, the two level histograms and the edge scanner (about 1.6 KiB) sat under the tone's 1 KiB
// state in one frame of the deepest stack chain (Trigger Midpoint -> poll -> summarize, GitHub #62).
// True when the edge pass ran and found edges active (ToneEstimate::wanted).
__attribute__((noinline)) bool bandsAndEdges(const FrameView &frame, Channel channel, bool withEdges, SignalSummary &result) {
    SignalBand bands[2];
    StateLevels levels[2];
    SignalBands::measure(frame, bands, withEdges ? &levels : nullptr);
    robust(result, bands[unsigned(channel)]);
    if (!withEdges) return false;
    EdgeScanner edges;
    const SignalBand thresholds = levels[unsigned(channel)].thresholds(bands[unsigned(channel)]); // One spike sets no threshold.
    edges.begin(thresholds.low, thresholds.high, EdgeScanner::laneBurst(frame.metadata()));
    edges.scan(frame, channel);
    if (const double candidate = edges.retryCandidate()) { edges.confirm(candidate); edges.scan(frame, channel); }
    edges.finish(frame.metadata().samplesPerSecond.value(), result);
    return edges.active();
}
__attribute__((noinline)) ToneResult tone(const FrameView &frame, Channel channel, double mean) {
    return ToneEstimate::measure(frame, channel, mean);
}
} // namespace
SignalSummary Analyzer::summarize(const FrameView &frame, Channel channel, bool withEdges, bool withBand) {
    SignalSummary result;
    const uint32_t count = frame.metadata().samples;
    dsp::Moments moments;
    if (!RecordMoments::measure(frame, channel, 0, count, moments) || !fill(result, moments, count))
        return {};
    result.hasPulses = withEdges; // No edge pass, no pulse count: not a zero.
    if (!withEdges && !withBand) return result;
    const bool edgesActive = bandsAndEdges(frame, channel, withEdges, result);
    if (withEdges) {
        const double rate = frame.metadata().samplesPerSecond.value();
        if (ToneEstimate::wanted(result, rate, edgesActive))
            ToneEstimate::apply(result, tone(frame, channel, result.mean), rate);
    }
    return result;
}
SignalSummary Analyzer::summarize(const int8_t *codes, uint32_t count, double samplesPerSecond, bool withEdges) {
    SignalSummary result;
    if (!codes || !count) return result;
    dsp::Moments moments;
    for (uint32_t i = 0; i < count; ++i) moments.add(codes[i]);
    fill(result, moments, count);
    result.hasPulses = withEdges;
    BandEstimate estimate; // Each code is a unit: one or two spiking codes set no threshold.
    StateLevels levels;
    for (uint32_t i = 0; i < count; ++i) { estimate.add(codes[i], codes[i]); levels.add(codes[i]); }
    const auto band = estimate.band();
    robust(result, band);
    if (withEdges) {
        EdgeScanner edges;
        const SignalBand thresholds = levels.thresholds(band);
        edges.begin(thresholds.low, thresholds.high);
        for (uint32_t i = 0; i < count; ++i) edges.feed(codes[i]);
        edges.close();
        if (const double candidate = edges.retryCandidate()) {
            edges.confirm(candidate);
            for (uint32_t i = 0; i < count; ++i) edges.feed(codes[i]);
            edges.close();
        }
        edges.finish(samplesPerSecond, result);
    }
    return result;
}
} // namespace openosc::scope
