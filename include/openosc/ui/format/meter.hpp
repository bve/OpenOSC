#pragma once
#include "openosc/measurement/dmm/display_prefix.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace openosc::ui {
struct MeterText {
    // The state caption of a DMM reading (Session::status, BarReading::status), one mapping for the
    // bar, the dial and the status pill: nothing for a live value. NoReading (the DMM's blanks,
    // dashes, "--.") is the controller's own "no number yet", not an error: "NO VALUE". INVALID is
    // left for a reading that has digits this firmware cannot read (Unsupported).
    static const char *caption(DataStatus status) {
        switch (status) {
        case DataStatus::Ok: return "";
        case DataStatus::Overload: return "OL";
        case DataStatus::Stale: return "STALE";
        case DataStatus::NoData: return "NO DATA";
        case DataStatus::NoReading: return "NO VALUE";
        default: return "INVALID";
        }
    }
    // The state in the alert colour: an overload or a reading this firmware cannot read. The link's
    // NO DATA / STALE are warnings, NoReading is neutral, a live value is none of them. One rule for
    // the pill's tone, the bar's caption and the dial's caption.
    static bool alert(DataStatus status) {
        return status != DataStatus::Ok && status != DataStatus::NoReading && status != DataStatus::Stale &&
               status != DataStatus::NoData;
    }
    // The shown reading's own state: Ok only for a usable value (an Ok status without one, as a
    // number of no known quantity, is Unsupported), else its status (NoReading, Overload, ...).
    static DataStatus state(const dmm::Result &result) {
        if (result.primary.valid()) return DataStatus::Ok;
        return result.primary.status == DataStatus::Ok ? DataStatus::Unsupported : result.primary.status;
    }
    // The digits to show: the value's text; OL for an overload; for NoReading the DMM's own segments
    // ("--.", dashes, or nothing), which the reading faces can draw ('-', '.'). Anything else
    // (no reading yet, an unreadable one): the neutral "----.--". Never the last number again.
    static const char *reading(const dmm::Result &result) {
        switch (state(result)) {
        case DataStatus::Ok: return result.mainText;
        case DataStatus::Overload: return "OL";
        case DataStatus::NoReading: return result.mainText; // Only '-' and '.' (Decoder::Digits::Blank).
        default: return "----.--";
        }
    }
    // Screen-only formatting: keep decoder, CSV, serial and saved-record text untouched. Strip
    // redundant zeros before the integer part of a valid reading, but keep one zero before a point.
    static bool displayReading(const dmm::Result &result, char *output, size_t capacity) {
        if (!output || !capacity) return false;
        const char *text = reading(result);
        const int copied = std::snprintf(output, capacity, "%s", text);
        if (copied < 0 || size_t(copied) >= capacity) return false;
        if (!result.primary.valid()) return true;

        const size_t sign = output[0] == '-' ? 1 : 0;
        size_t dot = sign;
        while (output[dot] && output[dot] != '.') ++dot;
        size_t firstNonZero = sign;
        while (firstNonZero < dot && output[firstNonZero] == '0') ++firstNonZero;
        const size_t keep = firstNonZero < dot ? firstNonZero : (dot > sign ? dot - 1 : sign);
        if (keep > sign) {
            std::memmove(output + sign, output + keep, std::strlen(output + keep) + 1);
        }
        return true;
    }
    // The digits and the display unit of a reading as the DMM screen shows them (main digits, the
    // active card): a resistance or capacitance value in the prefix of its magnitude
    // (dmm::DisplayPrefix, GitHub #168: 0.6700 MOhm -> "670.0" kΩ); anything else, an overload and a
    // blank as displayReading with the packet's unit (OL in its range's unit).
    static bool shown(const dmm::Result &result, char *output, size_t capacity, const char *&shownUnit) {
        shownUnit = unit(result.unit);
        const char *prefixed = nullptr;
        if (state(result) == DataStatus::Ok &&
            dmm::DisplayPrefix::of(result).format(result.primary.si, output, capacity, prefixed)) {
            shownUnit = unit(prefixed);
            return true;
        }
        return displayReading(result, output, capacity);
    }
    // Every unit the main reading can show (unit() of the packets' units and of the #168 prefixes).
    static constexpr const char *ReadingUnits[] = {"V", "mV", "uV", "A", "mA", "µA", "Ω", "kΩ", "MΩ",
                                                   "pF", "nF", "µF", "mF", "°C", "°F"};
    static const char *unit(const char *unit) {
        if (!std::strcmp(unit, "Ohm")) return "Ω";
        if (!std::strcmp(unit, "kOhm")) return "kΩ";
        if (!std::strcmp(unit, "MOhm")) return "MΩ";
        if (!std::strcmp(unit, "uA")) return "µA";
        if (!std::strcmp(unit, "uF")) return "µF";
        if (!std::strcmp(unit, "degC") || !std::strcmp(unit, "x")) return "°C";
        if (!std::strcmp(unit, "degF")) return "°F";
        return std::strlen(unit) <= 4 ? unit : "?";
    }
    // A scale number (the dial's and the bar's, GitHub #134): `value` counts at `decimals` (at most 6)
    // in the reading's unit, by integer arithmetic: trailing zeros dropped (0.50 -> 0.5, 1.000 -> 1),
    // a whole multiple of 1000 from 10000 up as "k". False and "": it does not fit.
    static bool scaleNumber(int value, unsigned decimals, char *output, size_t capacity) {
        if (!output || !capacity) return false;
        output[0] = 0;
        if (decimals > 6) return false;
        unsigned long divisor = 1;
        for (unsigned d = 0; d < decimals; ++d) divisor *= 10;
        const char *sign = value < 0 ? "-" : "";
        const auto magnitude = static_cast<unsigned long>(std::abs(value));
        unsigned long whole = magnitude / divisor, fraction = magnitude % divisor;
        int digits = int(decimals);
        while (digits && fraction % 10 == 0) { fraction /= 10; --digits; }
        const char *suffix = "";
        if (!digits && whole >= 10000 && whole % 1000 == 0) { whole /= 1000; suffix = "k"; }
        const int written = digits ? std::snprintf(output, capacity, "%s%lu.%0*lu%s", sign, whole, digits, fraction, suffix)
                                   : std::snprintf(output, capacity, "%s%lu%s", sign, whole, suffix);
        if (written < 0 || size_t(written) >= capacity) { output[0] = 0; return false; } // Never a cut number.
        return true;
    }
    static const char *function(const dmm::Result &result) {
        if (result.primary.quantity == Quantity::Temperature) return "Temperature";
        const bool current = result.primary.quantity == Quantity::Current;
        switch (result.kind) {
        case dmm::Kind::Dc: return current ? "DC current" : "DC voltage";
        case dmm::Kind::Ac: return current ? "AC current" : "AC voltage";
        case dmm::Kind::Resistance: return "Resistance";
        case dmm::Kind::Capacitance: return "Capacitance";
        case dmm::Kind::Diode: return "Diode";
        case dmm::Kind::DiodeWithBeeper: return "Diode + Beep"; // Both factory annunciators (docs/DMM_DIODE_BEEPER.md).
        case dmm::Kind::Continuity: return "Continuity";
        default: return "Measurement";
        }
    }
};
} // namespace openosc::ui
