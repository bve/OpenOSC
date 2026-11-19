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
    // An ADC-clipped record: ">" (true value higher), "<" (lower), "?" (either), or nullptr.
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
