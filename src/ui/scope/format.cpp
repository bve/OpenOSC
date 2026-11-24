#include "openosc/ui/scope/format.hpp"
#include "openosc/data/decimal.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace openosc::plot {
bool Format::quantity(double value, const char *unit, char *output, size_t capacity, unsigned precision, bool coarse, int prefer,
                      int *used) {
    if (!output || !capacity || !unit || !std::isfinite(value) || !precision || precision>8)
        return false;
    constexpr double powers[] = {1e-12, 1e-9, 1e-6, 1e-3, 1, 1e3, 1e6, 1e9};
    constexpr const char *prefixes[] = {"p", "n", "u", "m", "", "k", "M", "G"};
    double magnitude = std::abs(value);
    // Seconds from a minute on in minutes, from an hour on in hours ("1min", "1.5h"), never "3.6ks". The
    // unit is chosen by the value as rounded in it to the digits shown (a measured 59.9997 s/div is
    // "1min", not "60s"); the value itself is printed unrounded, rounded once.
    if (!std::strcmp(unit, "s") && magnitude > 0) {
        const auto rounded = [&](double x) {
            const double step = std::pow(10.0, std::floor(std::log10(x)) + 1 - double(precision));
            return std::floor(x / step + .5) * step;
        };
        if (rounded(magnitude / 3600) >= 1) { if (used) *used = 4; return quantity(value / 3600, "h", output, capacity, precision, coarse); }
        if (rounded(magnitude / 60) >= 1) { if (used) *used = 4; return quantity(value / 60, "min", output, capacity, precision, coarse); }
    }
    unsigned index = 4;
    if (magnitude > 0) {
        index = 0;
        while (index < 7 && magnitude >= powers[index + 1])
            ++index;
    }
    // Percent, degrees Celsius, minutes and hours take no prefix ("100m°C" reads as nonsense).
    const bool plain = !std::strcmp(unit, "%") || !std::strcmp(unit, "°C") || !std::strcmp(unit, "min") || !std::strcmp(unit, "h");
    if (plain) index = 4;
    double scaled = magnitude / powers[index];
    if (index < 7 && !plain) { // 999.97n rounds to 1000n: that is 1u.
        const unsigned whole = scaled >= 100 ? 3 : scaled >= 10 ? 2 : 1;
        const double step = std::pow(10.0, double(whole) - double(precision));
        if (std::floor(scaled / step + .5) * step >= 1000) { ++index; scaled = magnitude / powers[index]; }
    }
    if (!coarse && magnitude > 0 && prefer >= 0 && prefer < 8 && !plain &&
        ((prefer == int(index) - 1 && magnitude / powers[prefer] < 1050) || (prefer == int(index) + 1 && magnitude / powers[prefer] >= .95))) {
        index = unsigned(prefer); scaled = magnitude / powers[index];
    }
    char number[32];
    if (coarse && !std::strcmp(unit, "%") && magnitude < 10000) { // Percent has no prefix: fixed decimals instead.
        uint32_t divisor = 1;
        for (unsigned i = 0; i < precision; ++i) divisor *= 10;
        const uint64_t units = uint64_t(std::floor(magnitude * divisor + .5));
        // A value these decimals round to nothing keeps its digits in scientific form ("2e-3%"), never zero.
        if (magnitude > 0 && !units) { if (!Decimal::number(value, number, sizeof number, precision)) return false; }
        else if (!Decimal::fraction(units, divisor, number, sizeof number, 7)) return false;
        const int n = std::snprintf(output, capacity, "%s%s%%", value < 0 && number[0] != '-' && units ? "-" : "", number);
        return n > 0 && size_t(n) < capacity;
    }
    if (coarse && index < 7 && !plain) { // Three integer digits once rounded: 99.9m at 2 digits is 100m.
        const unsigned digits = scaled >= 100 ? 3 : scaled >= 10 ? 2 : 1;
        const double step = std::pow(10.0, -double(precision > digits ? precision - digits : 0));
        if (std::floor(scaled / step + .5) * step >= 100) { ++index; scaled = magnitude / powers[index]; }
    }
    if (scaled >= 10000 || (magnitude > 0 && scaled < .001)) {
        if (!Decimal::number(value, number, sizeof number, precision))
            return false;
        index = 4;
    } else {
        const unsigned digits=scaled>=1000 ? 4 : scaled>=100 ? 3 : scaled>=10 ? 2 : 1; // 4: a kept prefix ("1020k").
        unsigned decimals=precision>digits ? precision-digits : 0;
        if (scaled>0 && scaled<1) {
            double significant=scaled; unsigned leading=0;
            while (significant<1 && leading<8) { significant*=10; ++leading; }
            decimals=precision+leading-1;
        }
        if (decimals>7) {
            if (!Decimal::number(value,number,sizeof number,precision)) return false;
            index=4;
        } else {
        uint32_t divisor=1;
        for (unsigned i=0; i<decimals; ++i) divisor*=10;
        if (!Decimal::fraction(uint64_t(std::floor(scaled * divisor + .5)), divisor, number, sizeof number,
                               7))
            return false;
        }
    }
    const bool sign = value < 0 && number[0] != '-';
    if (used) *used = int(index);
    const int n = std::snprintf(output, capacity, "%s%s%s%s", sign ? "-" : "", number, prefixes[index], unit);
    return n > 0 && size_t(n) < capacity;
}
bool Format::steady(double seconds, unsigned precision, char *output, size_t capacity) {
    static constexpr struct { double seconds, next; const char *unit; } units[] = {
        {1e-3, 1000, "ms"}, {1, 60, "s"}, {60, 60, "min"}, {3600, 0, "h"}};
    if (!output || !capacity || !std::isfinite(seconds) || seconds <= 0 || !precision || precision > 6) return false;
    unsigned u = 0;
    while (u + 1 < sizeof units / sizeof *units && seconds >= units[u + 1].seconds) ++u;
    for (;;) {
        const double value = seconds / units[u].seconds;
        const unsigned digits = value >= 100 ? 3 : value >= 10 ? 2 : 1;
        unsigned decimals = precision > digits ? precision - digits : 0;
        unsigned long divisor = 1;
        for (unsigned i = 0; i < decimals; ++i) divisor *= 10;
        unsigned long n = static_cast<unsigned long>(std::lround(value * divisor));
        unsigned long whole = n / divisor;
        if (units[u].next && whole >= units[u].next) { ++u; continue; } // 59.96 s is 1.00min.
        if (decimals && whole >= (digits == 3 ? 1000UL : digits == 2 ? 100UL : 10UL)) { // 9.995 rounds to 10.00.
            --decimals; divisor /= 10; n = static_cast<unsigned long>(std::lround(value * divisor)); whole = n / divisor;
        }
        const int written = decimals ? std::snprintf(output, capacity, "%lu.%0*lu%s", whole, int(decimals), n % divisor, units[u].unit)
                                     : std::snprintf(output, capacity, "%lu%s", n, units[u].unit);
        return written > 0 && size_t(written) < capacity;
    }
}
void Format::points(uint32_t points, char *output, size_t capacity) {
    if (!output || !capacity) return;
    if (points && !(points % 1024)) std::snprintf(output, capacity, "%luK", static_cast<unsigned long>(points / 1024));
    else std::snprintf(output, capacity, "%lu", static_cast<unsigned long>(points));
}
// Integers only: the firmware links newlib-nano without float printf, where "%.1f" prints nothing.
void Format::compactPoints(uint32_t points, char *output, size_t capacity) {
    if (!output || !capacity) return;
    output[0] = 0;
    if (points < 1024) { Decimal::integer(points, output, capacity); return; }
    // The finest of 1/100, 1/10, 1 K that keeps three digits, rounded half up: 10234 points are
    // 9.994K -> "9.99K", 10235 are 9.995K -> "10K", 102349 are 99.95K -> "100K".
    uint32_t unit = 100;
    uint64_t scaled = (uint64_t(points) * unit + 512) / 1024;
    while (unit > 1 && scaled >= 1000) {
        unit /= 10;
        scaled = (uint64_t(points) * unit + 512) / 1024;
    }
    char text[24];
    const size_t length = Decimal::fraction(scaled, unit, text, sizeof text - 1, 2); // Drops trailing zeros.
    if (!length || length + 2 > capacity) return;
    std::memcpy(output, text, length);
    output[length] = 'K'; output[length + 1] = 0;
}
void Format::exactMillivolts(int32_t millivolts, char *output, size_t capacity) {
    const uint32_t size = millivolts < 0 ? uint32_t(-int64_t(millivolts)) : uint32_t(millivolts);
    std::snprintf(output, capacity, "%s%lu.%03luV", millivolts < 0 ? "-" : "", (unsigned long)(size / 1000), (unsigned long)(size % 1000));
}
} // namespace openosc::plot
