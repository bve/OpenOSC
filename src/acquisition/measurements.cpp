#include "openosc/acquisition/measurements.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
Measurements Measurements::calculate(const SignalSummary &s, const ChannelMetadata &c,const CounterReading *counter) {
    Measurements result;
    if (c.enabled && counter && counter->valid && std::isfinite(counter->hertz) && counter->hertz>=0) {
        result.values_[unsigned(Measurement::CounterFrequency)]=counter->hertz;
        result.valid_|=1U<<unsigned(Measurement::CounterFrequency);
    }
    if (!s.valid) return result;
    if (s.hasPulses) {
        result.values_[unsigned(Measurement::PulseCount)] = s.pulses;
        result.valid_ |= 1U << unsigned(Measurement::PulseCount);
    }
    if (s.hasDuty && std::isfinite(s.dutyPercent) && s.dutyPercent >= 0 && s.dutyPercent <= 100) {
        result.values_[unsigned(Measurement::Duty)] = s.dutyPercent;
        result.valid_ |= 1U << unsigned(Measurement::Duty);
    }
    if (s.hasFrequency && std::isfinite(s.frequencyHz) && s.frequencyHz > 0) {
        result.values_[unsigned(Measurement::Frequency)] = s.frequencyHz;
        result.values_[unsigned(Measurement::Period)] = 1 / s.frequencyHz;
        result.valid_ |= (1U << unsigned(Measurement::Frequency)) | (1U << unsigned(Measurement::Period));
        result.source_ = s.frequencySource; result.span_ = s.frequencySpan;
    }
    if (!c.calibrated || !c.enabled || !c.probeDenominator || !std::isfinite(c.zeroCode) ||
        !std::isfinite(c.voltsPerCode) || c.voltsPerCode <= 0) return result;
    const double factor = c.voltsPerCode * c.probeNumerator / c.probeDenominator;
    // ADC-clipped only where the band reaches a rail (BandEstimate drops up to Outliers columns): one
    // bus glitch at 0x7F/0x80 (the known one-word lane outliers) marks nothing; a clipped plateau does.
    // Without a band (a few points), the extremes themselves.
    result.clipLow_ = (s.hasRobust ? s.robustMinimum : s.minimum) <= ClipLow;
    result.clipHigh_ = (s.hasRobust ? s.robustMaximum : s.maximum) >= ClipHigh;
    result.values_[unsigned(Measurement::PeakToPeak)] = (s.maximum - s.minimum) * factor;
    result.values_[unsigned(Measurement::Mean)] = (s.mean - c.zeroCode) * factor;
    result.values_[unsigned(Measurement::Minimum)] = (s.minimum - c.zeroCode) * factor;
    result.values_[unsigned(Measurement::Maximum)] = (s.maximum - c.zeroCode) * factor;
    result.values_[unsigned(Measurement::Rms)] = std::sqrt(std::max(0.0,
        s.rms * s.rms - 2 * c.zeroCode * s.mean + c.zeroCode * c.zeroCode)) * factor;
    result.values_[unsigned(Measurement::AcRms)] = std::sqrt(std::max(0.0, s.rms * s.rms - s.mean * s.mean)) * factor;
    if (s.hasRobust) {
        result.values_[unsigned(Measurement::Amplitude)] = (s.robustMaximum - s.robustMinimum) * factor;
        result.values_[unsigned(Measurement::Top)] = (s.robustMaximum - c.zeroCode) * factor;
        result.values_[unsigned(Measurement::Bottom)] = (s.robustMinimum - c.zeroCode) * factor;
        for (const auto kind : {Measurement::Amplitude, Measurement::Top, Measurement::Bottom})
            if (std::isfinite(result.values_[unsigned(kind)])) result.valid_ |= 1U << unsigned(kind);
    }
    for (unsigned i = 0; i < unsigned(Measurement::Duty); ++i)
        if (i != unsigned(Measurement::Frequency) && i != unsigned(Measurement::Period) && std::isfinite(result.values_[i]))
            result.valid_ |= 1U << i;
    return result;
}
void Measurements::bound(Measurement kind, double uncertainty) {
    const auto i = unsigned(kind);
    if (i < Count && std::isfinite(uncertainty) && uncertainty > 0) bounds_[i] = float(uncertainty);
}
// The value's leading digit at 10^e, the uncertainty's at 10^k: digits down to 10^k, e - k + 1.
int Measurements::supported(Measurement kind) const {
    double v = 0;
    const double u = uncertainty(kind);
    if (!value(kind, v) || !(u > 0) || !(std::abs(v) > 0)) return int(MaximumDigits);
    return int(std::floor(std::log10(std::abs(v)))) - int(std::floor(std::log10(u))) + 1;
}
unsigned Measurements::digits(Measurement kind) const {
    return unsigned(std::clamp(supported(kind), int(MinimumDigits), int(MaximumDigits)));
}
const char *Measurements::clipMark(Measurement kind) const {
    switch (kind) {
    case Measurement::PeakToPeak: case Measurement::Amplitude: return clipLow_ || clipHigh_ ? ">" : nullptr;
    case Measurement::Maximum: case Measurement::Top: return clipHigh_ ? ">" : nullptr;
    case Measurement::Minimum: case Measurement::Bottom: return clipLow_ ? "<" : nullptr;
    case Measurement::Mean: case Measurement::Rms: case Measurement::AcRms: return clipLow_ || clipHigh_ ? "?" : nullptr;
    default: return nullptr;
    }
}
bool Measurements::approximate(Measurement kind) const { return supported(kind) < int(MinimumDigits); }
bool CounterCheck::suspect(const CounterReading &counter, const Measurements &record, uint64_t recordTimestampUs) {
    double hertz = 0;
    if (!counter.valid || !counter.gateUs || record.frequencySource() != FrequencySource::Edges || !(record.frequencySpan() > 0) ||
        !record.value(Measurement::Frequency, hertz))
        return false;
    // Milliseconds of the same clock, modular: the record before the count's end by `before`, its
    // windows spanning [startMs, endMs].
    const int32_t before = int32_t(counter.endMs - uint32_t(recordTimestampUs / 1000));
    const int64_t spanMs = uint32_t(counter.endMs - counter.startMs);
    if (before < -int32_t(SlackMs) || before > spanMs + SlackMs) return false;
    const double tolerance = hertz * (RelativeTolerance + 2 / record.frequencySpan()) + 2e6 / counter.gateUs;
    return std::abs(counter.hertz - hertz) > tolerance;
}
} // namespace openosc::scope
