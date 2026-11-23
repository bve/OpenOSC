#include "openosc/ui/components/readout_text.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/scope/format.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace openosc::ui {
namespace {
struct Step { unsigned precision; bool coarse; };
constexpr Step Steps[] = {{3, false}, {2, false}, {1, false}, {2, true}, {1, true}};
constexpr unsigned TimedDigits[] = {6, 5, 3, 4, 1}; // Timed FC falls back to fewer digits as the cell fills.
// The short tags, where a channel's three readouts need the room (Measurements::Tags order).
constexpr const char *ShortTags[] = {"f", "pp", "R", "T", "Av", "Mn", "Mx", "AC", "D", "n", "", "F", "Am", "Tp", "Bt"}; // FC: F, its mark kept ("Ff?").
static_assert(sizeof ShortTags / sizeof *ShortTags == scope::Measurements::Count);
// Form index: its step and whether it takes the short tag.
struct Shape { uint8_t step; bool shortTag; };
// Full tags keep two digits as long as they can (the next prefix: "RMS0.51V" before "R513mV").
constexpr Shape Shapes[ReadoutText::Forms - 1] = {{0, false}, {1, false}, {3, false}, {1, true}, {3, true}, {2, false}, {2, true}, {4, false}, {4, true}};
// The significant digits a formatted value shows, from its first non-zero digit to its unit or
// exponent: a bound's nines count (">99.9%": three), so do integer zeros ("100kHz": three).
unsigned significant(const char *text) {
    unsigned count = 0;
    bool started = false;
    for (const char *p = text; *p && *p != 'e'; ++p) {
        if (*p >= '0' && *p <= '9') { started = started || *p != '0'; count += started; }
        else if (*p != '.' && *p != '-' && *p != '>') break;
    }
    return count;
}
} // namespace
ReadoutText::Presentation ReadoutText::presentation(const scope::Measurements &values, scope::Measurement kind) {
    Presentation shown;
    const bool tone = (kind == scope::Measurement::Frequency || kind == scope::Measurement::Period) &&
                      values.frequencySource() == scope::FrequencySource::Spectrum;
    if (tone || values.approximate(kind)) shown.mark = "~";
    if (const char *clip = values.clipMark(kind)) shown.mark = clip;
    if (values.uncertainty(kind) > 0) shown.limit = values.digits(kind);
    return shown;
}
ReadoutIndicator ReadoutIndicator::of(scope::ReadoutBasis basis, const ReadoutState &state) {
    ReadoutIndicator indicator;
    if (basis != scope::ReadoutBasis::Record) return indicator;
    indicator.full = true;
    // A first pass shows how far it has come (never done while running); older values stay whole.
    const bool first = state.running && !state.older;
    indicator.percent = first ? uint8_t(state.percent < 99 ? state.percent : 99) : 100;
    indicator.stale = state.older && state.ageMs >= StaleMs;
    return indicator;
}
bool ReadoutText::fit(const char *tag, bool valid, double value, const char *unit, int width, char *output, size_t capacity,
                      const char *suffix) {
    const char *space = *tag ? " " : "";
    for (const auto step : Steps) {
        char formatted[24] = "--";
        if (valid && !plot::Format::quantity(value, unit, formatted, sizeof formatted, step.precision, step.coarse)) continue;
        for (const char *gap : {space, ""}) { // Dropping the space beats losing a digit.
            std::snprintf(output, capacity, "%s%s%s%s", tag, gap, formatted, suffix);
            if (Elements::textWidth(output) <= width) return true;
        }
        if (!valid) break;
    }
    // Nothing fits (a value far outside the instrument's range): say so in the cell, never clip.
    std::snprintf(output, capacity, "%s%sOVR", tag, space);
    return false;
}
unsigned ReadoutText::digits(unsigned index) { return index < Overflow ? Steps[Shapes[index].step].precision : 0; }
bool ReadoutText::form(unsigned kind, bool valid, double value, const char *mark, unsigned index, ReadoutParts &output, unsigned size,
                       unsigned limit, Prefer prefer, bool gap, const char *unit) {
    output = {};
    output.size = size;
    if (index >= Forms || kind >= scope::Measurements::Count || kind == unsigned(scope::Measurement::Off)) return false;
    const bool overflow = index == Overflow;
    const auto shape = overflow ? Shape{0, false} : Shapes[index];
    const char *units = unit ? unit : scope::Measurements::Units[kind];
    if (overflow) { // Only for a value no other form can write (far outside the instrument's range).
        for (unsigned i = 0; i < Overflow; ++i) if (form(kind, valid, value, mark, i, output, size, limit, prefer, gap, unit)) return false;
        output = {};
        output.size = size;
        std::snprintf(output.value, sizeof output.value, "OVR");
    } else if (valid) {
        const auto step = Steps[shape.step];
        // Bounded: significant digits at most `limit` (a coarse form's fixed decimals are checked below).
        const bool timed = kind == unsigned(scope::Measurement::CounterFrequency) && limit > scope::Measurements::MaximumDigits;
        const unsigned offered = timed ? TimedDigits[shape.step] : step.precision;
        const unsigned precision = limit && !step.coarse ? std::min(offered, limit) : offered;
        if (!plot::Format::quantity(value, units, output.value, sizeof output.value, precision, step.coarse,
                                    prefer.index, &output.prefix))
            return false;
        // A percent just under 100 that rounds to "100%" would read as DC: its bound instead, with
        // the decimals the form keeps (">99.9%" at three digits, ">99.99%" at two decimals). A form
        // with no decimal to spare (">99%") says too little of an exact value: not offered unbounded;
        // bounded to two digits it is all the value supports.
        if (std::strstr(output.value, "100") == output.value && std::abs(value) < 100 && !std::strcmp(units, "%")) {
            const unsigned nines = step.coarse ? precision : precision > 2 ? precision - 2 : 0;
            if (!nines && !limit) return false;
            char decimals[8] = "";
            for (unsigned i = 0; i < nines && i + 1 < sizeof decimals; ++i) decimals[i] = '9';
            std::snprintf(output.value, sizeof output.value, nines ? ">99.%s%%" : ">99%%", decimals);
        }
        if (limit && significant(output.value) > limit) return false;
    } else std::snprintf(output.value, sizeof output.value, "--");
    const bool shortTag = shape.shortTag || (unit && kind == unsigned(scope::Measurement::PeakToPeak));
    std::snprintf(output.tag, sizeof output.tag, "%s%s", shortTag ? ShortTags[kind] : scope::Measurements::Tags[kind], mark ? mark : "");
    output.tagWidth = Elements::textWidth(output.tag, size) + (gap && !shape.shortTag ? TagGap : 0);
    output.valueWidth = Elements::textWidth(output.value, size);
    return true;
}
} // namespace openosc::ui
