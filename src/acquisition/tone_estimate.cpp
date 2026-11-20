#include "openosc/acquisition/analysis.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <type_traits>

namespace openosc::scope {
namespace {
constexpr double Pi = 3.14159265358979323846;
// Log power, for the parabola through three samples of a peak.
double logPower(double power) { return std::log(std::max(power, 1e-30)); }
// The vertex of the parabola through (-1, a), (0, b), (1, c), within [-1, 1].
double vertex(double a, double b, double c) {
    const double divisor = a - 2 * b + c;
    if (!(divisor < 0)) return a > c ? -1 : c > a ? 1 : 0; // No maximum between: towards the larger.
    return std::clamp(.5 * (a - c) / divisor, -1.0, 1.0);
}
} // namespace
void ToneEstimate::begin(uint32_t samples, double mean) {
    // Reset in place: `*this = {}` built a 1 KiB temporary on the stack first (the Trigger Midpoint
    // chain through Analyzer::summarize then overran the ZT706S stack budget).
    static_assert(std::is_trivially_destructible_v<ToneEstimate>);
    new (this) ToneEstimate{};
    samples_ = samples; mean_ = mean;
    if (samples < MinimumSamples) { stage_ = Rounds + 1; return; }
    length_ = Coarse; segments_ = std::min<uint32_t>(CoarseSegments, samples / Coarse); filters_ = Bins;
    for (unsigned k = 0; k < Bins; ++k) coefficients_[k] = float(2 * std::cos(2 * Pi * (CoarseLow + k) / Coarse));
    segment();
}
// Segments spread evenly over the record: the first at its start, the last at its end.
void ToneEstimate::segment() {
    if (segment_ == segments_) {
        if (stage_ == 0) coarseDone(); else roundDone();
        return;
    }
    start_ = segments_ > 1 ? uint32_t(uint64_t(samples_ - length_) * segment_ / (segments_ - 1)) : 0;
    at_ = 0;
    std::fill(s1_, s1_ + filters_, 0.f); std::fill(s2_, s2_ + filters_, 0.f);
}
double ToneEstimate::lobe(unsigned bin) const {
    // A rectangular window's lobe: a tone's energy lies within a bin either side of its bin.
    return energy_ > 0 ? 2 * (power_[bin - 1] + power_[bin] + power_[bin + 1]) / (double(Coarse) * energy_) : 0;
}
double ToneEstimate::candidate(unsigned bin) const {
    const double offset = vertex(logPower(power_[bin - 1]), logPower(power_[bin]), logPower(power_[bin + 1]));
    return (CoarseLow + bin + std::clamp(offset, -.5, .5)) / Coarse;
}
void ToneEstimate::coarseDone() {
    unsigned best = 1;
    for (unsigned k = 2; k + 1 < Bins; ++k) if (power_[k] > power_[best]) best = k;
    result_.share = float(lobe(best));
    cycles_[0] = candidate(best); candidates_ = 1;
    if (!(result_.share >= Share)) {
        // No single tone: the strongest other lobe, which together with it must hold Share.
        unsigned second = 0;
        for (unsigned k = 1; k + 1 < Bins; ++k)
            if ((k + 3 <= best || k >= best + 3) && (!second || power_[k] > power_[second])) second = k;
        const double joint = second ? result_.share + lobe(second) : 0;
        if (!(joint >= Share)) { stage_ = Rounds + 1; return; }
        result_.share = float(joint);
        cycles_[1] = candidate(second); candidates_ = 2;
    }
    roundDone(); // stage_ 0: starts the first refinement.
}
// A refinement finished (or the coarse stage, stage_ 0): each candidate's vertex, then the next
// refinement or the result.
void ToneEstimate::roundDone() {
    if (stage_ > 0) {
        double share = 0;
        for (unsigned c = 0; c < candidates_; ++c) {
            const double *p = power_ + 3 * c;
            cycles_[c] += spacing_ * vertex(logPower(p[0]), logPower(p[1]), logPower(p[2]));
            shares_[c] = energy_ > 0 ? 2 * std::max({p[0], p[1], p[2]}) / (double(length_) * energy_) : 0;
            share += shares_[c];
        }
        result_.share = float(share);
    }
    if (stage_ == Rounds) {
        stage_ = Rounds + 1;
        if (candidates_ == 2) { harmonicDone(); return; }
        result_.cyclesPerSample = cycles_[0];
        result_.valid = result_.share >= Share && cycles_[0] > Lowest && cycles_[0] <= Highest;
        return;
    }
    ++stage_;
    length_ = FineFirst << (2 * (stage_ - 1));
    segments_ = length_ < samples_ ? std::min<uint32_t>(FineSegments, samples_ / length_) : 1;
    length_ = std::min(length_, samples_);
    spacing_ = .5 / length_; // Half a bin of this length.
    filters_ = uint8_t(3 * candidates_);
    for (unsigned k = 0; k < filters_; ++k) {
        coefficients_[k] = float(2 * std::cos(2 * Pi * (cycles_[k / 3] + (int(k % 3) - 1) * spacing_)));
        power_[k] = 0;
    }
    energy_ = 0; segment_ = 0;
    segment();
}
namespace {
// The distance (cycles per sample) from y of the nearest folded harmonic 2..Harmonics of x.
double harmonicMiss(double x, double y) {
    double miss = 1;
    for (unsigned k = 2; k <= ToneEstimate::Harmonics; ++k) {
        const double phase = k * x - std::floor(k * x), folded = std::min(phase, 1 - phase);
        miss = std::min(miss, std::abs(folded - y));
    }
    return miss;
}
} // namespace
void ToneEstimate::harmonicDone() {
    const double a = cycles_[0], b = cycles_[1], bin = 1.0 / length_;
    const bool aFundamental = harmonicMiss(a, b) <= CompatibleBins * bin && harmonicMiss(b, a) > RivalBins * bin;
    const bool bFundamental = harmonicMiss(b, a) <= CompatibleBins * bin && harmonicMiss(a, b) > RivalBins * bin;
    if (aFundamental == bFundamental) return; // Both, or neither: not one periodic signal's fundamental.
    const double f = aFundamental ? a : b;
    result_.cyclesPerSample = f;
    result_.valid = result_.share >= Share && f > Lowest && f <= Highest;
}
bool ToneEstimate::step(const FrameView &frame, Channel channel, uint32_t &budget) {
    while (stage_ <= Rounds) {
        const uint32_t weight = !stage_ ? CoarseWeight : candidates_ == 2 ? HarmonicWeight : FineWeight;
        if (!budget) return false;
        // A budget under a point's weight is held as credit: every point costs exactly its weight.
        const uint32_t n = uint32_t(std::min<uint64_t>(length_ - at_, (uint64_t(credit_) + budget) / weight));
        if (!n) { credit_ += budget; budget = 0; return false; }
        budget -= n * weight - credit_; credit_ = 0;
        const float mean = float(mean_);
        const unsigned filters = filters_;
        float energy = 0;
        frame.cursor(channel, start_ + at_).run(n, [&](int16_t code) {
            const float x = code - mean;
            energy += x * x;
            for (unsigned k = 0; k < filters; ++k) { const float s = x + coefficients_[k] * s1_[k] - s2_[k]; s2_[k] = s1_[k]; s1_[k] = s; }
        });
        energy_ += energy;
        at_ += n;
        if (at_ < length_) return false;
        for (unsigned k = 0; k < filters; ++k)
            power_[k] += double(s1_[k]) * s1_[k] + double(s2_[k]) * s2_[k] - double(coefficients_[k]) * s1_[k] * s2_[k];
        ++segment_;
        segment();
    }
    return true;
}
ToneResult ToneEstimate::measure(const FrameView &frame, Channel channel, double mean) {
    ToneEstimate tone;
    tone.begin(frame.metadata().samples, mean);
    uint32_t budget = UINT32_MAX;
    while (!tone.step(frame, channel, budget)) budget = UINT32_MAX;
    return tone.result();
}
namespace {
bool resolvedByEdges(const SignalSummary &s, double samplesPerSecond) {
    return s.hasFrequency && s.frequencyHz <= ToneEstimate::Lowest * samplesPerSecond;
}
} // namespace
bool ToneEstimate::wanted(const SignalSummary &s, double samplesPerSecond, bool edgesRan) {
    return edgesRan && s.valid && !resolvedByEdges(s, samplesPerSecond);
}
void ToneEstimate::apply(SignalSummary &s, const ToneResult &tone, double samplesPerSecond) {
    if (resolvedByEdges(s, samplesPerSecond)) return;
    const double toneHz = tone.valid ? tone.cyclesPerSample * samplesPerSecond : 0;
    if (s.hasFrequency && tone.valid && s.frequencyAgreement >= Regular && std::abs(s.frequencyHz - toneHz) <= Agreement * toneHz) {
        // Regular edges the tone confirms: their frequency and pulses. Their duty is not: under eight
        // points per cycle a crossing interpolated across a transition is off by a good part of a point.
        s.hasDuty = false; s.dutyPercent = 0;
        return;
    }
    const bool refused = s.hasFrequency; // An edge frequency over Fs/8 the tone did not confirm.
    s.hasFrequency = s.hasDuty = false;
    s.frequencyHz = s.dutyPercent = s.frequencySpan = s.frequencyAgreement = 0;
    s.frequencySource = FrequencySource::None;
    if (tone.valid && std::isfinite(toneHz) && toneHz > 0) {
        s.frequencyHz = toneHz; s.hasFrequency = true; s.frequencySource = FrequencySource::Spectrum;
    }
    // A refused edge frequency or a tone the edges did not follow: missed or extra cycles, the
    // crossings are not the pulses. Edges that gave no frequency at all keep their crossings, as
    // any record without a frequency does.
    if (refused || s.hasFrequency) { s.hasPulses = false; s.pulses = 0; }
}
} // namespace openosc::scope
