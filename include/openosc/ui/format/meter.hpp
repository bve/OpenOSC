#pragma once
#include "openosc/measurement/dmm/result.hpp"
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
