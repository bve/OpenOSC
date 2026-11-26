#pragma once
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/counter_window.hpp"

namespace openosc::scope {
// Top, Bottom and Amplitude (Top - Bottom): the robust levels, the extremes of the signal's band
// (BandEstimate: an isolated spike in one or two columns of a record, or one or two points of the
// screen series, left out; repeated pulses kept), chosen beside the exact Maximum, Minimum and Peak
// to peak, which stay every point's.
enum class Measurement : unsigned { Frequency, PeakToPeak, Rms, Period, Mean, Minimum, Maximum, AcRms, Duty, PulseCount, Off, CounterFrequency,
                                    Amplitude, Top, Bottom, Count };
class Measurements {
public:
    static constexpr unsigned Count = unsigned(Measurement::Count);
    static constexpr const char *Names[] = {"Frequency", "Peak to peak", "RMS", "Period", "Mean", "Minimum", "Maximum", "AC RMS", "Duty", "Pulses (rising edges)", "Off", "Hardware frequency / FCNT",
                                            "Amplitude (Top - Bottom)", "Top (robust high)", "Bottom (robust low)"};
    static constexpr const char *Tags[] = {"f", "Vpp", "RMS", "T", "Avg", "Min", "Max", "AC", "Duty", "n", "", "FC", "Amp", "Top", "Bot"}; // Readout names.
    static constexpr const char *Units[] = {"Hz", "V", "V", "s", "V", "V", "V", "V", "%", "", "", "Hz", "V", "V", "V"};
    static_assert(sizeof Names / sizeof *Names == unsigned(Measurement::Count) && sizeof Tags / sizeof *Tags == unsigned(Measurement::Count) &&
                  sizeof Units / sizeof *Units == unsigned(Measurement::Count));
    static Measurements calculate(const SignalSummary &summary, const ChannelMetadata &channel,const CounterReading *counter=nullptr);
    bool value(Measurement kind, double &output) const {
        const auto i = unsigned(kind);
        if (i >= Count || !(valid_ & (1U << i))) return false;
        output = values_[i]; return true;
    }
    // Where Frequency and Period come from (None when they are not valid) and, from the edges, the
    // samples their span covers (SignalSummary::frequencySpan).
    FrequencySource frequencySource() const { return source_; }
    double frequencySpan() const { return span_; }
    // GitHub #171 (CounterFallback): Frequency and Period from a hardware count, only where the
    // record has no frequency of its own (an edge or spectrum f stays); duty and pulses stay unknown.
    // False when the record has one or `hertz` is no frequency.
    bool fromCounter(double hertz, bool approximate);
    // A value its basis resolves only to `uncertainty` (absolute, its unit): ScreenReadouts bounds
    // frequency, period and duty by its column sampling. digits(): the significant digits the
    // value then supports, its last digit no finer than the uncertainty's leading one, between
    // MinimumDigits and MaximumDigits (MaximumDigits without a bound); approximate(): fewer than
    // MinimumDigits would be honest, the value is shown marked ("~").
    static constexpr unsigned MinimumDigits = 2, MaximumDigits = 3;
    void bound(Measurement kind, double uncertainty);
    double uncertainty(Measurement kind) const { return unsigned(kind) < Count ? bounds_[unsigned(kind)] : 0; }
    unsigned digits(Measurement kind) const;
    bool approximate(Measurement kind) const;
    // An ADC-clipped record: ">" (true value higher), "<" (lower), "?" (either), or nullptr. Clipped:
    // the robust band (SignalSummary::robust*) at a rail code, not a lone sample there.
    static constexpr int16_t ClipLow = -128, ClipHigh = 127;
    const char *clipMark(Measurement kind) const;
private:
    int supported(Measurement kind) const; // Significant digits the bound allows (MaximumDigits without one).
    double values_[Count]{};
    float bounds_[Count]{};
    double span_ = 0;
    unsigned valid_ = 0;
    bool clipLow_ = false, clipHigh_ = false;
    FrequencySource source_ = FrequencySource::None;
};
// GitHub #171, Analyze > Measurements > Short window f/T > From the counter: the f and T a window
// too short to resolve them (fewer than MinimumPeriods periods of the count) takes from the settled
// hardware count (CounterReading::settled, above zero) of the same moment as the record, in
// CounterCheck's clock (the record's timestampUs in ms against the count's [startMs, endMs],
// modular): none from a count that began more than CounterCheck::SlackMs after the record (a later
// moment: a HOLD or STOP record of the past, whatever counts now) or ended more than LatestMs
// before it (the longest gap between two counts); a count that ended over FreshMs before it, or is
// not Clean (Filtered, Unverified), is approximate ("~", CNT?). `windowSeconds`: the samples the
// basis covers (the visible window, or the whole record) at the record's rate.
class CounterFallback {
public:
    static constexpr double MinimumPeriods = 4;
    static constexpr uint32_t FreshMs = 500, LatestMs = 3000;
    enum class Use : uint8_t { None, Clean, Approximate };
    static Use judge(const CounterReading &counter, uint64_t recordTimestampUs, double windowSeconds);
    // Fills a missing f/T of `values` (Measurements::fromCounter); true when it did.
    static bool apply(Measurements &values, const CounterReading &counter, uint64_t recordTimestampUs, double windowSeconds);
};
// The hardware counter beside a record of the same signal: FCNT is suspect ("FC?") when it
// disagrees with the frequency the record's edges resolve, and only then. The record must be
// captured within the counter's windows ([startMs, endMs]) or SlackMs around them (a HOLD record of the past, a counter
// gate of another moment: no claim), its frequency from the edges (four points per cycle or more:
// a tone from the spectrum may be an alias), and the two apart by more than both resolutions:
// RelativeTolerance (ADC and source clocks, interpolation) plus two samples of the record's edge
// span, and two counts of the gate. The counter's value itself is never replaced.
class CounterCheck {
public:
    static constexpr uint32_t SlackMs = 500;
    static constexpr double RelativeTolerance = 3e-3;
    static bool suspect(const CounterReading &counter, const Measurements &record, uint64_t recordTimestampUs);
};
} // namespace openosc::scope
