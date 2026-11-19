#pragma once
#include "openosc/acquisition/readouts.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::ui {
// How a result relates to the record on display: still being computed (percent), or finished
// for an older record than the one shown (age).
struct ReadoutState {
    bool running=false, older=false;
    uint8_t percent=0;
    uint32_t ageMs=0;
};
// What the footer shows of the basis, without a text of its own. Screen (the calm default):
// nothing. Full: the rail under each channel's values, whole while the values on show are of a
// full pass. Only the first pass, with no values yet, lights it as far as it has covered the
// record: a pass refreshing older values (every record in RUN) keeps it whole, so RUN never
// empties and refills it. Stale when the values are of an older record than the one shown and
// were finished StaleMs or longer ago (a pass that keeps up refreshes them sooner).
struct ReadoutIndicator {
    static constexpr uint32_t StaleMs = 2000;
    bool full = false, stale = false;
    uint8_t percent = 0;
    static ReadoutIndicator of(scope::ReadoutBasis basis, const ReadoutState &state);
};
// A readout as two texts, its tag (with its mark) and its value, and their widths in the font of
// `size` (Elements::font): the footer draws them in two shades, so they need no space between them.
struct ReadoutParts {
    char tag[8] = "", value[24] = "";
    int tagWidth = 0, valueWidth = 0;
    unsigned size = 12;
    int prefix = -1; // Format::quantity's prefix index (-1: none).
    int width() const { return tagWidth + valueWidth; }
};
// Readout texts in IBM Plex Sans 12, never clipped. fit(): the first form within a width, at
// each precision (3 to 1) with, then without the space after the tag; then the next prefix ("Max
// -0.89V" for -888mV) or fixed-decimal percent, scientific where it would round to zero
// ("Duty2e-3%"). A value no form fits reads "Max OVR" (fit() is then false). "--" is a value that
// is not known.
class ReadoutText {
public:
    static bool fit(const char *tag, bool valid, double value, const char *unit, int width, char *output, size_t capacity,
                    const char *suffix = "");
    // A selected measurement (Measurements::Tags/Units) as form `index` of Forms, most precise first:
    // three digits, two, two at the next prefix or in fixed-decimal percent; then the same two
    // with the short tag ("Mx", "D", "F" for FC); then one digit, with each tag. mark follows the tag:
    // "~" a frequency or period from the spectrum (FrequencySource::Spectrum) or a value its basis
    // resolves to fewer than two digits (Measurements::approximate), "?" a suspect
    // counter. Last, Overflow: "OVR", only for a value no other form can write. A percent below
    // 100 that a form rounds to 100 reads as its bound (">99.9%", ">99.99%"), never "100%". False
    // for a form the value has not (an Off slot has none). Widths in the font of `size`.
    // limit: a bounded value's significant digits (Measurements::digits; 0: unbounded, the forms as
    // they are). No form then shows more, a coarse form's fixed decimals included ("12.35kHz" and
    // ">99.99%" are refused at 2 or 3), and ">99%" is that value's bound at two.
    static constexpr unsigned Forms = 10, Overflow = Forms - 1;
    static bool form(unsigned kind, bool valid, double value, const char *mark, unsigned index, ReadoutParts &output, unsigned size = 12,
                     unsigned limit = 0, int prefer = -1);
    // How a computed value reads in any layout (footer panels, Legacy): its mark ("~": a tone from
    // the spectrum, or approximate) and its digit limit (0 without a bound: Full, Vpp, ...).
    struct Presentation { const char *mark = ""; unsigned limit = 0; };
    static Presentation presentation(const scope::Measurements &values, scope::Measurement kind);
    static unsigned digits(unsigned index); // The significant digits form `index` keeps (0: OVR).
    static const char *basisName(scope::ReadoutBasis basis) { return basis==scope::ReadoutBasis::Record ? "Full" : "Scrn"; }
};
} // namespace openosc::ui
