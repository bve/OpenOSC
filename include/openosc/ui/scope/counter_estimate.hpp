#pragma once
#include "openosc/acquisition/readouts.hpp"
#include "openosc/ui/scope/options.hpp"
#include <cmath>
#include <cstdint>

namespace openosc::plot {
// Frequency counter > No edges counted > Trace f (~) (GitHub #172): the record's own frequency in the
// counter overlay and the FC cell, marked "~", where the hardware count is a Clean count of no edges
// (the trigger comparator never switched on a small fast signal). It stands in only when all hold:
// - the count is valid, Clean, 0 Hz with no edges (none(): not Changing, Uncertain or one edge);
// - the record's f comes from its edges (FrequencySource::Edges, with an edge span): a tone from the
//   spectrum may be an alias, and an f the readouts took from the counter is no record's own;
// - the record was sampled at 80 MS/s or more;
// - the record was captured from BeforeMs before the count began to AfterMs after it ended (the
//   longest gap between two counts plus CounterCheck::SlackMs, modular ms): a HOLD or Single record
//   of an earlier moment never stands in for a live count, and the value holds between counts.
// No clock correction applies (it is no count), and SCPI keeps the raw count.
class CounterEstimate {
public:
    static constexpr uint32_t MinimumSampleRateHz = 80000000;
    static constexpr int32_t BeforeMs = 1000;
    static constexpr uint32_t AfterMs = 2500 + scope::CounterCheck::SlackMs;
    struct Value {
        double hertz = 0;
        unsigned digits = 0; // The f readout's significant digits (Measurements::digits).
        bool valid() const { return hertz > 0; }
    };
    static bool none(const scope::CounterReading &r) {
        return r.valid && r.quality == scope::CounterQuality::Clean && r.hertz == 0 && r.edges == 0;
    }
    static bool within(const scope::CounterReading &r, uint64_t recordUs) {
        const int64_t after = int32_t(uint32_t(recordUs / 1000) - r.startMs); // From the count's start.
        return after >= -int64_t(BeforeMs) && after <= int64_t(uint32_t(r.endMs - r.startMs)) + AfterMs;
    }
    // Request frequency for an FC cell or the selected overlay even with no edge readout visible.
    static scope::ReadoutSelection selection(const scope::ReadoutSelection &shown, const CounterOptions &counter) {
        auto readouts = shown;
        if (!counter.estimate) return readouts;
        constexpr auto Fc = unsigned(scope::Measurement::CounterFrequency);
        for (unsigned c = 0; c < 2; ++c) {
            if (shown.needsEdges(c)) continue;
            bool counted = counter.source == c + 1;
            for (auto value : shown.values[c]) counted |= value == Fc;
            readouts.counterTrace[c] = counted;
        }
        return readouts;
    }
    static Value of(const scope::CounterReading &reading, const scope::ReadoutResult &result, unsigned channel) {
        if (channel > 1 || !none(reading) || !result.valid || !result.computed[channel] ||
            result.sampleRateHz < MinimumSampleRateHz || !within(reading, result.timestampUs)) return {};
        const auto &values = result.values[channel];
        double hertz;
        if (values.frequencySource() != scope::FrequencySource::Edges || !(values.frequencySpan() > 0) ||
            !values.value(scope::Measurement::Frequency, hertz) || !(hertz > 0) || !std::isfinite(hertz)) return {};
        return {hertz, values.digits(scope::Measurement::Frequency)};
    }
};
} // namespace openosc::plot
