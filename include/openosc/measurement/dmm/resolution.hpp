#pragma once
#include "openosc/measurement/dmm/result.hpp"
#include "openosc/measurement/dmm/temperature.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace openosc::dmm {
// The resolution a reading is shown with: its decimals in its own unit ("-15.561" mV: three decimals
// of mV, a quantum of 1 uV). Values shown beside it (MIN, MAX, AVG) use the same unit and quantum, so
// an average is never finer than the readings it averages. Rounding is for display only: the
// statistics keep their exact sums. A value with more digits there than the display has (a reading of
// another range in the same period) does not fit; the caller then shows it in its own magnitude.
// The rounding uses double (std::round); the text is written from integers only (%lu), because the
// firmware's printf has no floating point.
class Resolution {
public:
    static constexpr unsigned MaxDecimals = 6, MaxDigits = 5; // The display's five digits (25000/66000 counts; 9999).
    // The decimals the reading shows: the decoder's layout, else counted in its text; 255 unknown.
    static unsigned decimals(const Result &result) {
        if (result.decimals != 255) return result.decimals;
        const char *dot = std::strchr(result.mainText, '.');
        return dot ? unsigned(std::strlen(dot + 1)) : 0;
    }
    // The resolution of a valid reading; invalid (nothing to match) otherwise.
    static Resolution of(const Result &result) {
        Resolution resolution;
        NumericValue unit;
        const unsigned places = decimals(result);
        const bool temperature = result.primary.quantity == Quantity::Temperature;
        if (!result.primary.valid() || places > MaxDecimals || std::strlen(result.unit) >= sizeof resolution.unit_ ||
            (!temperature && (!UnitParser::parse("1", result.unit, unit) || !(unit.si > 0))))
            return resolution;
        resolution.decimals_ = places;
        resolution.unitSi_ = temperature ? (TypeK::fahrenheit(result.unit) ? 5. / 9 : 1) : unit.si;
        resolution.quantity_ = result.primary.quantity;
        std::strcpy(resolution.unit_, result.unit);
        return resolution;
    }
    bool valid() const { return quantity_ != Quantity::Unknown; }
    // One unit of the last digit shown, in SI ("-15.561" mV: 1e-6 V); 0 when invalid.
    double quantum() const {
        if (!valid()) return 0;
        double q = unitSi_;
        for (unsigned i = 0; i < decimals_; ++i) q /= 10;
        return q;
    }
    Quantity quantity() const { return quantity_; }
    unsigned places() const { return decimals_; }
    const char *unit() const { return unit_; }
    // `si` rounded to this quantum, in this unit ("-7.840mV"); a value that rounds to zero has no sign.
    // False when invalid, not finite or wider than MaxDigits here.
    bool format(double si, char *output, size_t capacity) const { return format(si, unit_, output, capacity); }
    // The same with the unit written as `unit` (a display form of unit(): "kΩ" for "kOhm").
    bool format(double si, const char *unit, char *output, size_t capacity, bool relative = false) const {
        if (!valid() || !output || !capacity || !unit || !std::isfinite(si)) return false;
        uint32_t scale = 1;
        for (unsigned i = 0; i < decimals_; ++i) scale *= 10;
        const double value = quantity_ == Quantity::Temperature ? TypeK::display(si, unit_, relative) : si / unitSi_;
        const double counts = std::round(value * scale);
        if (!(std::fabs(counts) < 1e5)) return false; // MaxDigits digits: 99999 at most.
        const auto magnitude = uint32_t(std::fabs(counts));
        const char *sign = counts < 0 && magnitude ? "-" : "";
        const unsigned long whole = magnitude / scale, fraction = magnitude % scale;
        const int written = decimals_
            ? std::snprintf(output, capacity, "%s%lu.%0*lu%s", sign, whole, int(decimals_), fraction, unit)
            : std::snprintf(output, capacity, "%s%lu%s", sign, whole, unit);
        return written > 0 && size_t(written) < capacity;
    }

private:
    unsigned decimals_ = 0;
    double unitSi_ = 0;
    Quantity quantity_ = Quantity::Unknown;
    char unit_[sizeof Result::unit]{};
};
} // namespace openosc::dmm
