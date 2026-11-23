#include "openosc/acquisition/bode.hpp"
#include "openosc/acquisition/bode_analysis.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/acquisition/timebases.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
namespace {
constexpr double Pi = 3.14159265358979323846;
// cos/sin of a phase advancing by `step` per point, seeded exactly at the chunk's first point.
struct Phasor {
    double c, s, dc, ds;
    Phasor(double first, double step) : c(std::cos(first)), s(std::sin(first)), dc(std::cos(step)), ds(std::sin(step)) {}
    void next() { const double nc = c * dc - s * ds; s = s * dc + c * ds; c = nc; }
};
double wrap(double degrees) {
    while (degrees > 180) degrees -= 360;
    while (degrees <= -180) degrees += 360;
    return degrees;
}
} // namespace

BodeGrid BodeGrid::of(const BodeSettings &s, uint64_t minimum, uint64_t maximum) {
    BodeGrid grid;
    if (minimum > maximum || !maximum) return grid; // No range at all (std::clamp needs minimum <= maximum).
    grid.start = std::clamp(s.startMillihertz, minimum, maximum);
    grid.stop = std::clamp(s.stopMillihertz, minimum, maximum);
    if (!grid.start || grid.stop <= grid.start || !s.pointsPerDecade) { grid.count = 0; return grid; }
    const double decades = std::log10(double(grid.stop) / double(grid.start));
    const double wanted = std::ceil(decades * s.pointsPerDecade - 1e-9) + 1;
    grid.count = unsigned(std::clamp(wanted, 2.0, double(BodeSettings::MaximumPoints)));
    return grid;
}
uint64_t BodeGrid::at(unsigned index) const {
    if (!count) return 0;
    if (index + 1 >= count) return stop;
    if (!index) return start;
    return uint64_t(std::llround(double(start) * std::pow(double(stop) / double(start), double(index) / (count - 1))));
}

void BodeTone::begin(const FrameMetadata &m, double frequencyHz, uint32_t limit) {
    *this = {};
    frequency_ = frequencyHz;
    const double rate = m.samplesPerSecond.value();
    const bool both = m.channels[0].enabled && m.channels[1].enabled;
    if (!both || m.layout == Layout::InterleavedCh1 || m.rolling || m.samples < 64 || !(rate > 0) || !(frequencyHz > 0)) {
        flags_ = BodePoint::Incomplete; return;
    }
    for (unsigned c = 0; c < 2; ++c) {
        const auto &channel = m.channels[c];
        if (!channel.calibrated || !(channel.voltsPerCode > 0) || !channel.probeDenominator || !channel.probeNumerator) {
            flags_ |= BodePoint::Uncalibrated; continue;
        }
        scale_[c] = channel.voltsPerCode * channel.probeNumerator / channel.probeDenominator;
    }
    length_ = std::min(m.samples, limit ? limit : m.samples);
    first_ = m.samples - length_; // The newest points: the longest after the settle.
    if (frequencyHz > MaximumFraction * rate) flags_ |= BodePoint::Alias;
    if (double(length_) * frequencyHz / rate < MinimumCycles) flags_ |= BodePoint::FewCycles;
    omega_ = 2 * Pi * frequencyHz / rate;
    hann_ = 2 * Pi / (length_ - 1);
}
// One pass over the points' pairs: the tone's phasor and the Hann weight once per point (seeded exactly
// at the span's first point, then rotated), shared by both channels and the window's own sums; the six
// products as fused multiply-adds (FPv5 VFMA.F64: one rounding each); the extremes in the same pass.
template <class Read> void BodeTone::pass(uint32_t first, uint32_t n, Read read) {
    Phasor tone(omega_ * first, omega_), window(hann_ * first, hann_);
    double w = 0, wc = 0, ws = 0, x0 = 0, c0 = 0, s0 = 0, x1 = 0, c1 = 0, s1 = 0;
    int16_t lo0 = sums_[0].low, hi0 = sums_[0].high, lo1 = sums_[1].low, hi1 = sums_[1].high;
    int16_t a = 0, b = 0;
    for (uint32_t i = 0; i < n && read(a, b); ++i) {
        const double weight = .5 - .5 * window.c;
        const double va = weight * a, vb = weight * b;
        w += weight; wc = std::fma(weight, tone.c, wc); ws = std::fma(weight, tone.s, ws);
        x0 += va; c0 = std::fma(va, tone.c, c0); s0 = std::fma(va, tone.s, s0);
        x1 += vb; c1 = std::fma(vb, tone.c, c1); s1 = std::fma(vb, tone.s, s1);
        lo0 = a < lo0 ? a : lo0; hi0 = a > hi0 ? a : hi0; lo1 = b < lo1 ? b : lo1; hi1 = b > hi1 ? b : hi1;
        tone.next(); window.next();
        ++at_;
    }
    w_ += w; wc_ += wc; ws_ += ws;
    sums_[0].x += x0; sums_[0].xc += c0; sums_[0].xs += s0; sums_[0].low = lo0; sums_[0].high = hi0;
    sums_[1].x += x1; sums_[1].xc += c1; sums_[1].xs += s1; sums_[1].low = lo1; sums_[1].high = hi1;
}
void BodeTone::accumulate(uint32_t first, const int16_t *ch1, const int16_t *ch2, uint32_t n) {
    if (!ch1 || !ch2 || first != at_) return; // In order, from where the last one ended.
    uint32_t i = 0;
    pass(first, n, [&](int16_t &a, int16_t &b) { a = ch1[i]; b = ch2[i]; ++i; return true; });
}
bool BodeTone::step(const FrameView &frame, uint32_t budget) {
    if (done()) return true;
    if (!frame.pairs()) { flags_ |= BodePoint::Incomplete; return true; } // Both channels in one word (Linear, FourBanks).
    const uint32_t n = std::min(budget ? budget : 1u, length_ - at_);
    auto cursor = frame.pairCursor(first_ + at_);
    const uint32_t before = at_;
    pass(at_, n, [&](int16_t &a, int16_t &b) { return cursor.next(a, b); });
    if (at_ - before != n) flags_ |= BodePoint::Incomplete; // The record ended early.
    return done();
}
double BodeTone::peakCodes(unsigned c) const {
    if (c > 1 || !(w_ > 0)) return 0;
    const auto &s = sums_[c];
    const double mean = s.x / w_;
    return std::max(double(s.high) - mean, mean - double(s.low));
}
BodePoint BodeTone::result() const {
    BodePoint p;
    p.frequencyHz = float(frequency_);
    p.flags = flags_;
    if (flags_ & (BodePoint::Incomplete | BodePoint::Uncalibrated | BodePoint::Alias | BodePoint::FewCycles)) return p;
    double amplitude[2], phase[2];
    for (unsigned c = 0; c < 2; ++c) {
        const auto &s = sums_[c];
        const double mean = s.x / w_, re = s.xc - mean * wc_, im = s.xs - mean * ws_;
        amplitude[c] = 2 * std::sqrt(re * re + im * im) / w_;
        phase[c] = std::atan2(-im, re) * 180 / Pi; // x = A cos(wn + phase).
        if (s.high >= ClipHigh || s.low <= ClipLow) p.flags |= c ? BodePoint::ClipCh2 : BodePoint::ClipCh1;
    }
    if (amplitude[0] < WeakReferenceCodes) p.flags |= BodePoint::WeakCh1;
    if (amplitude[1] < WeakOutputCodes) p.flags |= BodePoint::WeakCh2;
    p.ch1Vpp = float(2 * amplitude[0] * scale_[0]);
    if (!(amplitude[1] > 0) || !(amplitude[0] > 0)) { p.flags |= BodePoint::NoTone; return p; }
    p.gainDb = float(20 * std::log10(amplitude[1] * scale_[1] / (amplitude[0] * scale_[0])));
    p.phaseDeg = float(wrap(phase[1] - phase[0]));
    return p;
}

// Within the sampled timebases: the sweep's records are never an averaged Roll (Timebases::averaging).
uint64_t BodeSweep::timebaseFor(double frequencyHz) {
    if (!(frequencyHz > 0)) return Timebases::SampledMaximum;
    const double period = 1e9 / frequencyHz; // One cycle per division: about ten on the screen.
    for (auto value : Timebases::Values) if (double(value) >= period) return std::min(value, Timebases::SampledMaximum);
    return Timebases::SampledMaximum;
}
bool BodeSweep::start(const BodeSettings &settings, const BodeGrid &grid, const ScopeRequest &base, BodePoint *points) {
    if (!grid.valid() || !points || grid.count > BodeSettings::MaximumPoints || running()) return false;
    settings_ = settings; grid_ = grid; request_ = base; points_ = points;
    for (unsigned i = 0; i < grid.count; ++i) points[i] = {};
    request_.frontend.enabled[0] = request_.frontend.enabled[1] = true;
    // GitHub #87: both inputs AC for the whole sweep, set once here so every point's capture (and each
    // range change) keeps them; a DUT's DC level would otherwise clip a range the tone fits in. The
    // user's couplings are the original's: restored with it.
    request_.frontend.coupling[0] = request_.frontend.coupling[1] = Coupling::Ac;
    request_.trigger.mode = TriggerMode::Auto;
    request_.trigger.channel = Channel::Ch1;
    request_.rollAuto = false;
    // The smallest record (two channels of 32768): BodeTone reads the newest 32768 points, which must hold
    // the ten cycles timebaseFor() puts on the record; at 512K one of 262144 would leave about 1.25 of
    // them (FewCycles). The user's depth is the original's: restored with it.
    request_.recordPoints = RecordBudget::Default;
    settings_.averages = std::clamp<uint32_t>(settings_.averages, 1, BodeSettings::MaximumAverages);
    index_ = adjustments_ = 0;
    records_ = 0; mean_ = {}; taken_ = retriedDrive_ = false;
    failure_ = Failure::None;
    lease_.release();
    state_ = State::Tune;
    return true;
}
void BodeSweep::cancel() {
    lease_.release();
    if (running()) state_ = State::Cancelled;
}
void BodeSweep::finishPoint(const BodePoint &point) {
    points_[index_] = point;
    points_[index_].range[0] = uint8_t(request_.frontend.range[0]);
    points_[index_].range[1] = uint8_t(request_.frontend.range[1]);
    ++index_; adjustments_ = 0; records_ = 0; mean_ = {}; retriedDrive_ = false;
    state_ = index_ < grid_.count ? State::Tune : State::Done;
}
bool BodeSweep::arm(BodePort &port) {
    request_.timebaseNs = timebaseFor(actualHz_);
    if (!port.configure(request_)) { fail(Failure::Capture); return false; }
    settledUs_ = port.nowUs() + uint64_t(settings_.settleMs) * 1000;
    deadlineUs_ = settledUs_ + recordWaitUs();
    state_ = State::Wait;
    return true;
}
bool BodeSweep::restartPoint(BodePort::Drive drive) {
    if (drive != BodePort::Drive::Underrun || retriedDrive_) {
        fail(drive == BodePort::Drive::Underrun ? Failure::Underrun : Failure::Unconfirmed);
        return false;
    }
    // The fault can fall inside the current record. Discard this point's entire average and
    // start the same frequency with a fresh settle. A persistent fault ends the sweep.
    lease_.release();
    retriedDrive_ = true;
    adjustments_ = records_ = 0;
    mean_ = {};
    state_ = State::Tune;
    return true;
}
uint64_t BodeSweep::recordWaitUs() const {
    const double recordUs = double(request_.timebaseNs) * 10 / 1000;
    return uint64_t(std::max(double(MinimumWaitMs) * 1000, WaitRecords * recordUs));
}
void BodeSweep::Mean::add(const BodePoint &p) {
    const double h = std::pow(10.0, double(p.gainDb) / 20), phase = double(p.phaseDeg) * Pi / 180;
    re += float(h * std::cos(phase)); im += float(h * std::sin(phase));
    magnitude += float(h); vpp += p.ch1Vpp;
    flags |= p.flags; ++valid;
}
BodePoint BodeSweep::Mean::of(const BodePoint &last, unsigned wanted) const {
    BodePoint p = last;
    if (!valid) return p;                              // Nothing to average: the record's own reasons.
    if (valid < wanted) p.flags |= BodePoint::Partial;
    if (valid == 1) return p;                          // Exactly that record (no round trip).
    p.flags = flags | (valid < wanted ? BodePoint::Partial : 0);
    p.ch1Vpp = vpp / valid;
    // A weak CH2's phase is noise: its complex mean would shrink towards zero with more records, a
    // deeper attenuation than measured; the mean magnitude keeps the floor where one record puts it.
    const double h = flags & BodePoint::WeakCh2 ? double(magnitude) / valid : std::hypot(double(re), double(im)) / valid;
    if (!(h > 0)) { p.flags |= BodePoint::NoTone; p.gainDb = p.phaseDeg = 0; return p; } // Cancelled out.
    p.gainDb = float(20 * std::log10(h));
    p.phaseDeg = flags & BodePoint::WeakCh2 ? 0.f : float(wrap(std::atan2(double(im), double(re)) * 180 / Pi));
    return p;
}
bool BodeSweep::step(BodePort &port) {
    if (state_ == State::Wait || state_ == State::Measure) {
        const auto drive = port.drive();
        if (drive != BodePort::Drive::Playing) return restartPoint(drive);
    }
    switch (state_) {
    case State::Tune:
        if (adjustments_) return arm(port); // The same frequency, confirmed: only the capture again.
        if (!port.tune(grid_.at(index_))) { fail(Failure::Generator); return false; }
        deadlineUs_ = port.nowUs() + uint64_t(MaximumTuneMs) * 1000;
        state_ = State::Tuning;
        [[fallthrough]]; // A generator that takes it at once (the DAC) is confirmed in the same step.
    case State::Tuning:
        switch (port.tuned(actualHz_)) {
        case BodePort::Tuning::Ready:
            if (!(actualHz_ > 0)) { fail(Failure::Unconfirmed); return false; }
            return arm(port);
        case BodePort::Tuning::Failed: fail(Failure::Unconfirmed); return false;
        case BodePort::Tuning::Pending: break;
        }
        if (port.nowUs() >= deadlineUs_) { fail(Failure::Unconfirmed); return false; }
        return true;
    case State::Wait: {
        if (port.frame(lease_)) {
            const auto &m = lease_.view().metadata();
            const bool again = taken_ && m.sequence == takenSequence_ && m.timestampUs == takenUs_;
            if (m.timestampUs >= settledUs_ && !again) {
                takenSequence_ = m.sequence; takenUs_ = m.timestampUs; taken_ = true;
                tone_.begin(m, actualHz_);
                state_ = State::Measure;
                return true;
            }
        }
        lease_.release();
        if (port.nowUs() >= deadlineUs_) {
            if (records_) finishPoint(mean_.of(points_[index_], settings_.averages)); // What it has.
            else { BodePoint none; none.frequencyHz = float(actualHz_); none.flags = BodePoint::NoFrame; finishPoint(none); }
        }
        return running();
    }
    case State::Measure: {
        if (!tone_.step(lease_.view(), StepBudget)) return true;
        lease_.release();
        const BodePoint point = tone_.result();
        bool adjusted = false;
        if (adjustments_ < MaximumAdjustments && !(point.flags & (BodePoint::Incomplete | BodePoint::Uncalibrated |
                                                                  BodePoint::Alias | BodePoint::FewCycles))) {
            for (unsigned c = 0; c < 2; ++c) {
                auto &range = request_.frontend.range[c];
                const bool clipped = point.flags & (c ? BodePoint::ClipCh2 : BodePoint::ClipCh1);
                if (clipped && range + 1u < VoltageRanges::Count) { ++range; adjusted = true; }
                else if (!clipped && tone_.peakCodes(c) < 24 && range > 0) { --range; adjusted = true; }
            }
        }
        if (adjusted) { // The same frequency again, from nothing: what was taken was at the old range.
            ++adjustments_; records_ = 0; mean_ = {}; state_ = State::Tune; return true;
        }
        ++records_;
        if (point.valid()) mean_.add(point);
        if (point.valid() || !mean_.valid) points_[index_] = point; // The last valid (else any) record.
        if (records_ >= settings_.averages) { finishPoint(mean_.of(points_[index_], settings_.averages)); return running(); }
        deadlineUs_ = port.nowUs() + recordWaitUs(); // The next distinct record, at the same settings.
        state_ = State::Wait;
        return true;
    }
    default: return false;
    }
}

BodeMarkers BodeMarkers::of(const BodePoint *points, unsigned count) {
    BodeMarkers m;
    m.peak = m.lower = m.upper = m.phase0 = m.phase180 = m.total = uint16_t(count);
    if (!points) return m;
    const auto shape = BodeShape::of(points, count);
    m.peak = shape.center; m.notch = shape.notch;
    // Of the two points a bound is crossed between, the one nearer its level.
    const auto nearer = [&](unsigned outer, int direction, float level) {
        if (outer >= count) return uint16_t(count);
        const unsigned inner = outer - direction;
        return uint16_t(std::fabs(points[inner].gainDb - level) < std::fabs(points[outer].gainDb - level) ? inner : outer);
    };
    m.lower = nearer(shape.lower, -1, shape.lowerLevel);
    m.upper = nearer(shape.upper, 1, shape.upperLevel);
    // Phase crossings between neighbouring points with a phase (valid, CH2 not weak): through 0, and
    // through +-180 (the wrap); an invalid or weak point between them is a gap nothing crosses.
    unsigned previous = count;
    for (unsigned i = 0; i < count; ++i) {
        if (!points[i].phased()) { previous = count; continue; }
        if (previous < count) {
            const float a = points[previous].phaseDeg, b = points[i].phaseDeg;
            if (m.phase0 == count && ((a > 0) != (b > 0)) && std::fabs(a - b) < 180) m.phase0 = uint16_t(i);
            if (m.phase180 == count && std::fabs(a - b) >= 180) m.phase180 = uint16_t(i);
        }
        previous = i;
    }
    return m;
}
void BodeCursor::step(int points, unsigned measured) {
    marker = BodeMarkers::None;
    if (!measured) { point = 0; return; }
    const int next = int(point) + points;
    point = unsigned(std::clamp(next, 0, int(measured) - 1));
}
void BodeCursor::page(int pages, unsigned measured) { step(pages * int(PageRows), measured); }
bool BodeCursor::jump(bool next, const BodePoint *points, unsigned measured) {
    const auto markers = BodeMarkers::of(points, measured);
    unsigned name = marker;
    for (unsigned k = 0; k < BodeMarkers::Named; ++k) {
        name = next ? name % BodeMarkers::Named + 1 : name <= 1 ? BodeMarkers::Named : name - 1;
        const unsigned at = markers.at(BodeMarkers::Name(name));
        if (at < measured) { marker = BodeMarkers::Name(name); point = at; return true; }
    }
    return false;
}
void BodeCursor::follow(unsigned before, unsigned measured) {
    if (!measured) { point = 0; marker = BodeMarkers::None; return; }
    if (point + 1 >= before) { point = measured - 1; marker = BodeMarkers::None; }
}
} // namespace openosc::scope
