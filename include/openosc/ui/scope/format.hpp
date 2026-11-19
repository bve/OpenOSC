#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::plot {
class Format {
public:
    // coarse: a value of three integer digits once rounded takes the next prefix up ("-888mV" is
    // "-0.89V" at precision 2, 99.9mV is "0.1V"), and a percentage keeps `precision` fixed decimals,
    // one too small for them scientific ("2e-3%", never 0%): the narrowest forms a tight readout
    // falls back to.
    // prefer: a prefix index (used: the one taken) kept within 5% past its range ("1020kHz", "0.98MHz").
    static bool quantity(double value, const char *unit, char *output, size_t capacity, unsigned precision = 4,
                         bool coarse = false, int prefer = -1, int *used = nullptr);
    // "64K" for a whole number of 1024 points, the plain count otherwise.
    static void points(uint32_t points, char *output, size_t capacity);
    // A count in at most five characters: exact below 1024, else three significant digits of
    // K = 1024, rounded half up in integer arithmetic ("512K", "48.8K", "9.76K"), as the record
    // budgets are named. No float printf: newlib-nano in the firmware has none.
    static void compactPoints(uint32_t points, char *output, size_t capacity);
    // Whole millivolts as volts, every millivolt shown ("3.300V", "-123.456V", "0.005V"): an entered
    // value such as a calibration reference, never rounded to the readouts' precision. Integer
    // arithmetic (no float printf).
    static void exactMillivolts(int32_t millivolts, char *output, size_t capacity);
};
} // namespace openosc::plot
