#pragma once
#include "openosc/ui/components/readout_text.hpp"
#include "openosc/ui/components/scope_footer_layout.hpp"

namespace openosc::ui {
// One panel's readouts (up to two) fitted into its panel's text width: never clipped, never
// overlapping, at least Gap apart. All start at their most precise forms and the widest steps
// down a form at a time (two digits, the short tag, one digit, coarse) until they fit together.
// Aligned when those forms fit their own cells (ScopeFooterLayout::CellWidth, the last taking the
// rest): each at its cell's left, so a value changing width does not move the others; otherwise
// packed, the spare room spread between them. Three that fit IBM Plex Sans 12 only below two
// digits each (Avg, Min and Max of tens of negative millivolts) take its condensed 11-pixel cut
// together when that keeps more (Narrow); so do Min and Max of a sub-millivolt negative noise
// floor beside a filtered suspect counter ("Ff?"). Beyond that, a safeguard values in the
// instrument's range do not reach: the widest left keep only their tag with "..." for the value.
class ReadoutRow {
public:
    static constexpr unsigned Items = ScopeFooterLayout::PerChannel;
    static constexpr int Gap = ScopeFooterLayout::CellGap;
    static constexpr const char *Elided = "...";
    struct Item {
        unsigned kind = unsigned(scope::Measurement::Off);
        bool valid = false;
        double value = 0;
        const char *mark = "";
        unsigned limit = 0; // Significant digits of a bounded value (ReadoutText::form); 0: unbounded.
        int prefer = -1;    // The prefix to keep near its range's edge (Format::quantity).
    };
    struct Placed {
        ReadoutParts parts;
        int x = 0; // From the left of the panel's text.
        unsigned form = 0; // ReadoutText's form index.
        bool shown = false, elided = false;
    };
    static constexpr unsigned Size = 12, NarrowSize = 11; // Elements::font sizes.
    // Hysteresis: a cell keeps its last form, and takes a more precise one only with this much room to spare.
    static constexpr int Spare = 6;
    struct Held { unsigned kind = ~0U, form = 0; int prefix = -1; }; // A cell's last (the caller keeps them).
    enum class Fit : uint8_t { Aligned, Packed, Narrow, Elided };
    // width: the panel's text; cell: one cell's width (the last takes what remains). Each value is
    // stepped down to its own cell first, so values keep their places; packed only past that.
    static Fit fit(const Item (&items)[Items], int width, int cell, Placed (&out)[Items], Held (*held)[Items] = nullptr);

private:
    static Fit in(const Item (&items)[Items], int width, int cell, unsigned size, bool gap, Placed (&out)[Items], const Held (*held)[Items]);
    // With TagGap unless it costs; both passes from the same held forms.
    static Fit best(const Item (&items)[Items], int width, int cell, unsigned size, Placed (&out)[Items], const Held (*held)[Items]);
};
} // namespace openosc::ui
