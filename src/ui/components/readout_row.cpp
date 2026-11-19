#include "openosc/ui/components/readout_row.hpp"
#include "openosc/ui/components/elements.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::ui {
namespace {
// The first form at or after `from` the value has, narrower than `than` (any, for than < 0), within
// the item's digit limit.
bool narrower(const ReadoutRow::Item &item, unsigned from, int than, unsigned size, unsigned &index, ReadoutParts &parts) {
    for (unsigned i = from; i < ReadoutText::Forms; ++i)
        if (ReadoutText::form(item.kind, item.valid, item.value, item.mark, i, parts, size, item.limit, item.prefer) &&
            (than < 0 || parts.width() < than)) {
            index = i;
            return true;
        }
    return false;
}
// How much a fitted row keeps: fewer values elided first, then the fewest digits of any value.
struct Kept {
    unsigned elided = 0, digits = 3;
    explicit Kept(const ReadoutRow::Placed (&placed)[ReadoutRow::Items]) {
        for (const auto &p : placed) {
            if (!p.shown) continue;
            elided += p.elided;
            if (!p.elided && ReadoutText::digits(p.form) < digits) digits = ReadoutText::digits(p.form);
        }
    }
    bool better(const Kept &b) const { return elided != b.elided ? elided < b.elided : digits > b.digits; }
};
} // namespace
ReadoutRow::Fit ReadoutRow::fit(const Item (&items)[Items], int width, int cell, Placed (&out)[Items], Held (*held)[Items]) {
    const auto fit = in(items, width, cell, Size, out, held);
    // held: the regular size's forms only; the narrow pass starts fresh.
    if (held)
        for (unsigned i = 0; i < Items; ++i) (*held)[i] = {items[i].kind, out[i].form, out[i].parts.prefix};
    const Kept regular(out);
    if (!regular.elided && regular.digits >= 2) return fit; // Two digits or more each in IBM Plex Sans 12.
    Placed narrow[Items];
    const auto condensed = in(items, width, cell, NarrowSize, narrow, nullptr);
    if (!Kept(narrow).better(regular)) return fit;
    std::copy(narrow, narrow + Items, out);
    return condensed == Fit::Elided ? Fit::Elided : Fit::Narrow;
}
ReadoutRow::Fit ReadoutRow::in(const Item (&items)[Items], int width, int cell, unsigned size, Placed (&out)[Items],
                               const Held (*held)[Items]) {
    unsigned shown = 0, forms[Items]{};
    const auto room = [&](unsigned i) { return i + 1 == Items ? width - int(i) * cell : cell - Gap; };
    for (unsigned i = 0; i < Items; ++i) {
        out[i] = {};
        unsigned start = 0;
        if (held && (*held)[i].kind == items[i].kind) { // The last form, one more precise only with room to spare.
            start = (*held)[i].form;
            unsigned index; ReadoutParts parts;
            if (start && narrower(items[i], start - 1, -1, size, index, parts) && index < start && parts.width() <= room(i) - Spare)
                start = index;
        }
        out[i].shown = narrower(items[i], start, -1, size, forms[i], out[i].parts) ||
                       (start && narrower(items[i], 0, -1, size, forms[i], out[i].parts));
        out[i].form = forms[i];
        shown += out[i].shown;
    }
    // Each down to its own cell where it can be: values keep their places.
    for (unsigned i = 0; i < Items; ++i) {
        unsigned index; ReadoutParts parts;
        while (out[i].shown && out[i].parts.width() > room(i) &&
               narrower(items[i], forms[i] + 1, out[i].parts.width(), size, index, parts)) {
            forms[i] = index; out[i].parts = parts; out[i].form = index;
        }
    }
    const int gaps = shown > 1 ? int(shown - 1) * Gap : 0;
    const auto total = [&]() {
        int sum = gaps;
        for (const auto &placed : out) if (placed.shown) sum += placed.parts.width();
        return sum;
    };
    // The widest steps down a form at a time until they fit.
    while (total() > width) {
        int widest = -1, at = -1;
        for (unsigned i = 0; i < Items; ++i) {
            unsigned index;
            ReadoutParts parts;
            if (out[i].shown && out[i].parts.width() > widest && narrower(items[i], forms[i] + 1, out[i].parts.width(), size, index, parts)) {
                widest = out[i].parts.width(); at = int(i);
            }
        }
        if (at < 0) break;
        narrower(items[at], forms[at] + 1, out[at].parts.width(), size, forms[at], out[at].parts);
        out[at].form = forms[at];
    }
    Fit fit = Fit::Packed;
    while (total() > width) { // Still too wide at their narrowest: the widest keeps its tag only.
        int widest = -1, at = -1;
        for (unsigned i = 0; i < Items; ++i)
            if (out[i].shown && !out[i].elided && out[i].parts.width() > widest) { widest = out[i].parts.width(); at = int(i); }
        if (at < 0) break;
        out[at].elided = true;
        std::snprintf(out[at].parts.value, sizeof out[at].parts.value, "%s", Elided);
        out[at].parts.valueWidth = Elements::textWidth(Elided, size);
        fit = Fit::Elided;
    }
    // Aligned when these forms fit their own cells (the last takes the rest of the width): in place.
    bool aligned = fit == Fit::Packed;
    for (unsigned i = 0; i < Items && aligned; ++i) {
        const bool last = i + 1 == Items;
        const int room = last ? width - int(i) * cell : cell - Gap;
        aligned = !out[i].shown || out[i].parts.width() <= room;
    }
    if (aligned) {
        for (unsigned i = 0; i < Items; ++i) out[i].x = int(i) * cell;
        return Fit::Aligned;
    }
    // Packed: the room left goes evenly between them; the first at the left edge.
    const int spare = width - total(), spaces = shown > 1 ? int(shown) - 1 : 1;
    int x = 0, k = 0;
    for (auto &placed : out) {
        if (!placed.shown) continue;
        placed.x = x + (shown > 1 ? spare * k / spaces : 0);
        x += placed.parts.width() + Gap;
        ++k;
    }
    return fit;
}
} // namespace openosc::ui
