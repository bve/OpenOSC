#pragma once
#include <cstdint>

namespace openosc::scope {
struct Timebases {
    // The slowest timebase the ADC is clocked for. Slower ones (to Maximum) Roll only: the ADC keeps
    // this one's sample plan and Roll stores the mean of averaging() consecutive points as one.
    static constexpr uint64_t SampledMaximum = 20000000000ULL, Maximum = 3600000000000ULL;
    // Nanoseconds/division; 64 bits are required for 5 s/div and slower. Past SampledMaximum each value
    // is a whole multiple of it, so its averaged points are exactly a tenth of a division apart in time.
    static constexpr uint64_t Values[] = {10, 25, 50, 100, 250, 500,
        1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000,
        1000000, 2000000, 5000000, 10000000, 20000000, 50000000, 100000000,
        200000000, 500000000, 1000000000, 2000000000, 5000000000ULL,
        10000000000ULL, 20000000000ULL, 60000000000ULL, 120000000000ULL, 300000000000ULL,
        600000000000ULL, 1200000000000ULL, 1800000000000ULL, 3600000000000ULL};
    // ADC points per stored Roll point (1 up to SampledMaximum), and the timebase the ADC is planned for.
    static constexpr uint32_t averaging(uint64_t ns) {
        return ns > SampledMaximum ? uint32_t((ns + SampledMaximum - 1) / SampledMaximum) : 1U;
    }
    static constexpr uint64_t sampled(uint64_t ns) { return ns / averaging(ns); }
    static uint64_t step(uint64_t current, int direction) {
        if (direction > 0) { for (auto value : Values) if (value > current) return value; }
        else if (direction < 0) {
            for (unsigned i = sizeof Values / sizeof *Values; i-- > 0;) if (Values[i] < current) return Values[i];
        }
        return current;
    }
    // Nearest listed value on a log scale.
    static uint64_t nearest(double ns) {
        uint64_t best = Values[0];
        for (unsigned i = 1; i < sizeof Values / sizeof *Values; ++i)
            if (ns * ns >= double(Values[i - 1]) * double(Values[i])) best = Values[i];
        return best;
    }
    static constexpr bool exact() {
        for (auto value : Values) if (value > SampledMaximum && value % SampledMaximum) return false;
        return Values[sizeof Values / sizeof *Values - 1] == Maximum;
    }
};
static_assert(Timebases::exact(), "Averaged timebases are whole multiples of the sampled maximum, the last one Maximum");
} // namespace openosc::scope
