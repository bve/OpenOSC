#pragma once
#include "openosc/measurement/dmm/resolution.hpp"
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace openosc::dmm {
// The unit a resistance or a capacitance is shown in (GitHub #168): the prefix of the value's own
// magnitude, as on a benchtop meter, whatever range the meter reports it in. Screen text only: the
// Result, its overload aliases, the bar and the dial, serial, SCPI and both CSV files keep the
// packet's unit and digits.
// - The value is first rounded to the packet's quantum (Resolution::quantum, a power of ten), so
//   the digits are the meter's own. The prefix then makes 1 <= |value| < 1000 where it can:
//   Ohm, kOhm or MOhm; pF, nF or uF, with uF the largest (22.00 mF -> 22000 uF).
// - Decimals: the quantum's own in that prefix, never finer (0.6700 MOhm -> 670.0 kOhm). A quantum
//   coarser than the prefix's unit pads whole zeros (0.0009 MOhm, 100 Ohm a step -> 900 Ohm).
// - Zero is shown in the unit one quantum would have (0.0000 MOhm, 100 Ohm a step -> 0 Ohm), so a
//   reading about zero keeps its unit. A negative value (REL) by its magnitude, never "-0".
// - Continuity reads Ohm but keeps its packet text: only the resistance and capacitance functions.
// The choice is the integer counts' alone, so the same reading always shows the same text.
class DisplayPrefix {
public:
    static constexpr int8_t Unknown = INT8_MIN;
    // The power of ten a quantum is (1e-6 -> -6); Unknown when it is none (degF, invalid).
    static int8_t exponent(double quantum) {
        if (!(quantum > 0) || !std::isfinite(quantum)) return Unknown;
        const long e = std::lround(std::log10(quantum));
        if (e < -18 || e > 18 || std::fabs(scaled(1, -int(e)) * quantum - 1) > 1e-9) return Unknown;
        return int8_t(e);
    }
    // A valid resistance or capacitance reading of its own function; invalid for anything else.
    static DisplayPrefix of(const Result &result) {
        DisplayPrefix prefix;
        const auto kind = functionKind(result.kind);
        const auto quantity = result.primary.quantity;
        if (!(kind == Kind::Resistance && quantity == Quantity::Resistance) &&
            !(kind == Kind::Capacitance && quantity == Quantity::Capacitance)) return prefix;
        return prefix.at(quantity, exponent(Resolution::of(result).quantum()));
    }
    // The same quantity at another quantum (a statistic's samples: Statistics' exponents).
    DisplayPrefix at(int8_t exponent) const { return at(quantity_, exponent); }
    bool valid() const { return quantity_ != Quantity::Unknown; }
    Quantity quantity() const { return quantity_; }
    int exponent() const { return exponent_; }
    // `si` rounded to the quantum, as its sign, digits and point ("670.0", "-3", "99990"), and its
    // unit in the packet's spelling ("kOhm", "pF", "uF"; MeterText::unit shows it). False when
    // invalid, not finite or wider than the display's five digits.
    bool format(double si, char *number, size_t capacity, const char *&unit) const {
        if (!valid() || !number || !capacity || !std::isfinite(si)) return false;
        const double counts = std::round(scaled(si, -exponent_));
        if (!(std::fabs(counts) < 1e12)) return false;
        const auto magnitude = uint64_t(std::fabs(counts));
        int digits = 1;
        for (uint64_t rest = magnitude; rest >= 10; rest /= 10) ++digits;
        const int top = exponent_ + digits - 1; // The leading digit's power of ten (zero: one quantum's).
        const Step *steps = quantity_ == Quantity::Resistance ? Ohms : Farads;
        const Step *step = steps;
        for (unsigned i = 1; i < Steps; ++i) if (steps[i].exponent <= top) step = steps + i;
        uint64_t shown = magnitude;
        for (int e = exponent_; e > step->exponent && shown < Limit; --e) shown *= 10; // Whole zeros.
        if (shown >= Limit) return false;
        const int decimals = exponent_ < step->exponent ? step->exponent - exponent_ : 0;
        unsigned long divisor = 1;
        for (int i = 0; i < decimals; ++i) divisor *= 10;
        const char *sign = counts < 0 && magnitude ? "-" : "";
        const auto whole = (unsigned long)(shown / divisor), fraction = (unsigned long)(shown % divisor);
        const int written = decimals ? std::snprintf(number, capacity, "%s%lu.%0*lu", sign, whole, decimals, fraction)
                                     : std::snprintf(number, capacity, "%s%lu", sign, whole);
        if (written <= 0 || size_t(written) >= capacity) return false;
        unit = step->unit;
        return true;
    }

private:
    struct Step { int exponent; const char *unit; };
    static constexpr unsigned Steps = 3;
    static constexpr uint64_t Limit = 100000; // Resolution::MaxDigits digits.
    static constexpr Step Ohms[Steps] = {{0, "Ohm"}, {3, "kOhm"}, {6, "MOhm"}};
    static constexpr Step Farads[Steps] = {{-12, "pF"}, {-9, "nF"}, {-6, "uF"}};
    // value x 10^e by exact powers of ten (1e12 is exact in double; a quotient by 10^n, not 10^-n).
    static double scaled(double value, int e) {
        double power = 1;
        for (int i = 0; i < (e < 0 ? -e : e); ++i) power *= 10;
        return e < 0 ? value / power : value * power;
    }
    DisplayPrefix at(Quantity quantity, int8_t exponent) const {
        DisplayPrefix prefix;
        if (exponent == Unknown || (quantity != Quantity::Resistance && quantity != Quantity::Capacitance)) return prefix;
        prefix.quantity_ = quantity;
        prefix.exponent_ = exponent;
        return prefix;
    }
    Quantity quantity_ = Quantity::Unknown;
    int8_t exponent_ = 0;
};
} // namespace openosc::dmm
