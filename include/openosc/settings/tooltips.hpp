#pragma once
#include <cstdint>

namespace openosc {
// The kind of a transient notice (ApplicationView::notify, GitHub #108). Each call site names its kind;
// Settings > Display > Tooltips turns each of the first five off on its own. Critical notices always
// show: hardware or a measurement not confirmed, factory calibration missing or replaced by defaults,
// low battery and power off, failures that lose data or change the signal output, and boot, settings
// and storage failures. A hidden notice changes nothing else: the action, its refusal and its outcome
// are the same.
enum class Tooltip : uint8_t {
    Usage,       // How to use a feature: "Thermocouple: set Manual junction in MENU".
    Unavailable, // An action refused by the normal restrictions: "Select Scope first".
    Progress,    // An operation under way: "Saving waveform...".
    Done,        // An outcome as expected: "Trigger centered", a file saved.
    Advisory,    // A side effect worth knowing: "XY needs CH1 + CH2; Y-T active".
    Critical,    // Always shown.
};
// Preferences::tooltipsHidden: a bit per optional kind (bit n: Tooltip(n) hidden). 0, the default and
// what files without the setting load as, shows every kind.
struct Tooltips {
    static constexpr unsigned Optional = unsigned(Tooltip::Critical); // The kinds a user can hide.
    static constexpr uint8_t Mask = uint8_t((1U << Optional) - 1U);
    static constexpr bool valid(uint32_t hidden) { return hidden <= Mask; }
    static constexpr bool shown(uint8_t hidden, Tooltip kind) {
        return kind >= Tooltip::Critical || !((hidden >> unsigned(kind)) & 1U);
    }
    static constexpr uint8_t set(uint8_t hidden, Tooltip kind, bool show) {
        const uint8_t bit = uint8_t(kind < Tooltip::Critical ? 1U << unsigned(kind) : 0U);
        return uint8_t(show ? hidden & ~bit : hidden | bit);
    }
};
} // namespace openosc
