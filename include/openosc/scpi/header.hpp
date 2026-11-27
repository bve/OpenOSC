#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::scpi {
class Header {
public:
    static bool matches(const char *actual, const char *pattern, unsigned *suffix = nullptr);
    static bool word(const char *actual, const char *pattern);
    static bool boolean(const char *argument, bool &value);
    // A quoted bare file name at `text` (1..size-1 characters, no slash or backslash), copied to `name`; `text` moves
    // past the closing quote. 0 or the SCPI error: -109 missing, -104 not a string, -224 not a bare name.
    static int quoted(const char *&text, char *name, size_t size);
    enum class Unit { Scalar, Voltage, Time, Frequency, Percent };
    static bool number(const char *argument, double &value, Unit unit = Unit::Scalar);
};
} // namespace openosc::scpi
