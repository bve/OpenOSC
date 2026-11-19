#pragma once
#include <cstdint>

namespace openosc::scope {
struct Timebases {
    // Nanoseconds/division; 64 bits are required for 5/10/20 s/div.
    static constexpr uint64_t Values[] = {10, 25, 50, 100, 250, 500,
        1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000,
        1000000, 2000000, 5000000, 10000000, 20000000, 50000000, 100000000,
        200000000, 500000000, 1000000000, 2000000000, 5000000000ULL,
        10000000000ULL, 20000000000ULL};
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
};
} // namespace openosc::scope
