#pragma once
#include <cstdint>

namespace openosc::generator {
// The generator screen's Left/Right frequency steps (GitHub #38): a decade grid of d x 10^k millihertz,
// d 1..9 (1, 2 ... 9 Hz, 10, 20 ... 90 Hz, 100 ... 900 Hz, 1, 2 ... 9 kHz, ...; from 1 mHz on the DAC).
// up() is the next grid value above, down() the next below, both within [minimum, maximum]: a grid
// value stepped up and down again (or down and up) comes back exactly, 1 kHz -> 2 kHz -> 1 kHz. An
// off-grid frequency (an exact edit, a level's or a run mode's limit) steps to its neighbours on the
// grid; the limits themselves are steps when they are not on it. The exact editor sets any value.
struct FrequencySteps {
    static uint64_t up(uint64_t millihertz, uint64_t minimum, uint64_t maximum) {
        if (millihertz < minimum) return minimum;
        const uint64_t decade = below(millihertz);
        const uint64_t next = (millihertz / decade + 1) * decade;
        return next < maximum ? next : maximum;
    }
    static uint64_t down(uint64_t millihertz, uint64_t minimum, uint64_t maximum) {
        if (millihertz > maximum) return maximum;
        if (millihertz <= 1) return minimum;
        const uint64_t lower = millihertz - 1, decade = below(lower);
        const uint64_t next = lower / decade * decade;
        return next > minimum ? next : minimum;
    }

private:
    static uint64_t below(uint64_t value) { // The largest power of ten <= value (value >= 1).
        uint64_t decade = 1;
        while (decade <= value / 10) decade *= 10;
        return decade;
    }
};
} // namespace openosc::generator
