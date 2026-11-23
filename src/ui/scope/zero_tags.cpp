#include "openosc/ui/scope/plot_frame.hpp"
#include <algorithm>
#include <utility>

namespace openosc::plot {
void ZeroTags::place(const int (&zero)[Count], const bool (&shown)[Count], int plotHeight, int (&centre)[Count]) {
    const int half = PlotFrame::TagHeight / 2, spacing = PlotFrame::TagHeight + PlotFrame::TagGap;
    const int top = -PlotFrame::Top + half, bottom = plotHeight - 1 + PlotFrame::Bottom - half;
    unsigned order[Count]{}, tags = 0;
    for (unsigned c = 0; c < Count; ++c) {
        centre[c] = zero[c] < 0 ? top + PlotFrame::ArrowHeight : zero[c] >= plotHeight ? bottom - PlotFrame::ArrowHeight
                                : std::clamp(zero[c], top, bottom);
        if (shown[c]) order[tags++] = c;
    }
    // Upper to lower by level (the lower index above on a tie): an insertion sort, no buffer.
    for (unsigned i = 1; i < tags; ++i)
        for (unsigned k = i; k && centre[order[k]] < centre[order[k - 1]]; --k) std::swap(order[k], order[k - 1]);
    // Each run of tags at most a tag apart with two closer than that: a tag apart around its middle,
    // inside the margin; a run that then reaches its neighbour joins it on the next pass.
    for (unsigned pass = 0; pass < Count; ++pass) {
        bool moved = false;
        for (unsigned first = 0, last = 0; first < tags; first = ++last) {
            bool close = false;
            int sum = centre[order[first]];
            for (; last + 1 < tags && centre[order[last + 1]] - centre[order[last]] <= spacing; ++last) {
                close = close || centre[order[last + 1]] - centre[order[last]] < spacing;
                sum += centre[order[last + 1]];
            }
            if (!close) continue;
            const int count = int(last - first + 1);
            // An arrow beyond the run's end tag keeps its room at that end.
            const int low = top + (zero[order[first]] < 0 ? PlotFrame::ArrowHeight : 0);
            const int high = bottom - (zero[order[last]] >= plotHeight ? PlotFrame::ArrowHeight : 0);
            const int start = std::clamp(sum / count - (count - 1) * spacing / 2, low, high - (count - 1) * spacing);
            for (unsigned k = first; k <= last; ++k) centre[order[k]] = start + int(k - first) * spacing;
            moved = true;
        }
        if (!moved) return;
    }
}
} // namespace openosc::plot
