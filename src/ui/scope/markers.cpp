#include "openosc/ui/scope/painter.hpp"
#include "openosc/ui/scope/tag_digits.hpp"
#include <algorithm>

namespace openosc::plot {
// Position markers sit in the PlotFrame margins (x < 0, x > w, y < 0 in plot coordinates), never
// on the signal; only their dashed guides cross the plot. A level outside the plot shows as an
// arrow at the margin's end instead of a pointer.
void Painter::markers() {
    const auto &o = model_.options();
    const int w = model_.viewport().width - 1, h = model_.viewport().height - 1;
    if (w < 20 || h < 20)
        return;
    if (o.mode == Mode::XY) {
        text("XY", w - 20, 3, palette_.readout, 20);
        return;
    }
    const auto &traces = model_.storage().traces;
    constexpr auto math = MathView::TraceIndex;
    for (unsigned c = 0; c < 2; ++c)
        if (traces[c].enabled) {
            const auto color = palette_.channel(c);
            const int slot = int(palette_.view.slot(c)); // Each mark's row or column: its on-screen slot's.
            if (traces[c].adcClipped)
                text(slot ? "2 CLIP" : "1 CLIP", w - 44, 14 + slot * 14, color, 44);
            if (traces[c].clippedTop)
                text("^", w - 12 - slot * 12, 0, color, 12);
            if (traces[c].clippedBottom)
                text("v", w - 12 - slot * 12, h - 13, color, 12);
        }
    if (o.showZero) {
        const int zero[2] = {traces[0].zero, traces[1].zero};
        const bool shown[2] = {traces[0].enabled, traces[1].enabled};
        int centre[2];
        ZeroTags::place(zero, shown, h + 1, centre);
        for (unsigned c = 0; c < 2; ++c)
            if (shown[c]) zeroTag(c, zero[c], centre[c], h);
        if (o.showsMath() && traces[math].enabled)
            zeroTag(math, traces[math].zero, ZeroTags::mathCentre(traces[math].zero, h + 1, shown, centre), h);
    }
    if (o.showsMath() && traces[math].enabled) {
        if (traces[math].clippedTop) text("^", w - 36, 0, palette_.math, 12);
        if (traces[math].clippedBottom) text("v", w - 36, h - 13, palette_.math, 12);
    }
    const unsigned source = unsigned(model_.triggerChannel());
    if (model_.metadata().rolling || !o.showTrigger || !model_.metadata().channels[source].enabled || !o.channels[source].visible)
        return;
    const auto color = palette_.triggerColor(source); // The Trigger role, else the source channel's colour.
    // Trigger level: right margin, pointing at the plot edge.
    const int right = w + 1, outer = w + PlotFrame::Right;
    const int y = model_.triggerY();
    const int offPlotHeight = trigger_.pulse ? 7 : 5;
    if (y < 0) triangle({int16_t(right), int16_t(offPlotHeight)}, {int16_t(outer), int16_t(offPlotHeight)}, {int16_t((right + outer) / 2), 0}, color);
    else if (y > h) triangle({int16_t(right), int16_t(h - offPlotHeight)}, {int16_t(outer), int16_t(h - offPlotHeight)}, {int16_t((right + outer) / 2), int16_t(h)}, color);
    else {
        line(0, y, w, y, color, 65, true);
        const int half = trigger_.pulse ? 7 : 4; // A press against a limit: taller, still only in the margin.
        triangle({int16_t(outer), int16_t(y - half)}, {int16_t(right), int16_t(y)}, {int16_t(outer), int16_t(y + half)}, color);
    }
    if (trigger_.limit) limitBadge(std::clamp(y, 0, h), trigger_.limit, h, color);
    // Trigger position: top margin, pointing down at the plot edge. An untriggered record (Auto's
    // newest points, an edge Noise reject refused) marks its configured pretrigger point (#65).
    if (!model_.triggerPlaced()) return;
    const int x = model_.triggerX(), top = -PlotFrame::Top, edge = -1;
    if (x < 0) triangle({6, int16_t(top)}, {6, int16_t(edge)}, {0, int16_t((top + edge) / 2)}, color);
    else if (x > w) triangle({int16_t(w - 6), int16_t(top)}, {int16_t(w - 6), int16_t(edge)}, {int16_t(w), int16_t((top + edge) / 2)}, color);
    else {
        line(x, 0, x, h, color, 55, true);
        triangle({int16_t(x - 4), int16_t(top)}, {int16_t(x + 4), int16_t(top)}, {int16_t(x), int16_t(edge)}, color);
    }
}
// A warning triangle with "!" in the right margin, on the side of the marker away from its limit
// (below an upper one, above a lower one), the other side where the plot has no room. Its ink is the
// TagText role on the trigger colour, as on the zero tags.
void Painter::limitBadge(int y, int limit, int h, uint32_t color) {
    constexpr int Height = 10, Gap = 8; // Gap: clear of the marker, also while it pulses (7).
    const int right = model_.viewport().width, outer = right + PlotFrame::Right - 1, middle = (right + outer) / 2;
    const int below = y + Gap, above = y - Gap - Height + 1;
    const int top = limit > 0 ? (below + Height - 1 <= h ? below : above) : (above >= 0 ? above : below);
    const int base = top + Height - 1;
    triangle({int16_t(middle), int16_t(top)}, {int16_t(outer + 1), int16_t(base + 1)}, {int16_t(right), int16_t(base + 1)}, color);
    const auto ink = palette_.tagInk(color);
    const int x = area_.x1 + middle, y0 = area_.y1 + top;
    fill({x, y0 + 3, x, y0 + 6}, ink); // The stroke and the dot of "!".
    fill({x, y0 + 8, x, y0 + 8}, ink);
}
// The channel's zero: a box with its digit in the left margin at `centre` and a tip to the plot
// edge at the zero itself (or, for a zero above or below the plot, an arrow that way).
void Painter::zeroTag(unsigned c, int zero, int centre, int h) {
    const bool math = c == MathView::TraceIndex;
    const auto color = math ? palette_.math : palette_.channel(c);
    const unsigned digit = palette_.view.slot(c); // The on-screen number, as its colour.
    const int left = -PlotFrame::Left, half = PlotFrame::TagHeight / 2, tip = left + PlotFrame::TagWidth;
    fill({area_.x1 + left, area_.y1 + centre - half, area_.x1 + tip - 1, area_.y1 + centre + half}, color);
    // The digit: TagDigits' raster, centred (two tag pixels on each side), one fill per run.
    static constexpr uint8_t plus[] = {0b1010000, 0b1110010, 0b1110111, 0b1010010, 0b1010000};
    static constexpr uint8_t minus[] = {0b1010000, 0b1110000, 0b1110111, 0b1010000, 0b1010000};
    const auto &glyph = model_.options().math == Math::Add ? plus : minus;
    const int width = math ? 7 : TagDigits::Width, height = math ? 5 : TagDigits::Height;
    const int x0 = left + (PlotFrame::TagWidth - width) / 2, y0 = centre - height / 2;
    const uint32_t ink = palette_.tagInk(color);
    const auto pixel = [&](int x, int y) { return math ? ((glyph[y] >> (6 - x)) & 1U) != 0 : TagDigits::ink(digit, x, y); };
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width;) {
            if (!pixel(x, y)) { ++x; continue; }
            const int start = x;
            while (x < width && pixel(x, y)) ++x;
            fill({area_.x1 + x0 + start, area_.y1 + y0 + y, area_.x1 + x0 + x - 1, area_.y1 + y0 + y}, ink);
        }
    const int middle = (left + tip - 1) / 2, arrow = PlotFrame::ArrowHeight;
    if (zero < 0)
        triangle({int16_t(left), int16_t(centre - half - 1)}, {int16_t(tip - 1), int16_t(centre - half - 1)},
                 {int16_t(middle), int16_t(centre - half - arrow)}, color);
    else if (zero > h)
        triangle({int16_t(left), int16_t(centre + half + 1)}, {int16_t(tip - 1), int16_t(centre + half + 1)},
                 {int16_t(middle), int16_t(centre + half + arrow)}, color);
    else
        triangle({int16_t(tip), int16_t(centre - 3)}, {int16_t(tip), int16_t(centre + 3)}, {int16_t(-1), int16_t(zero)}, color);
}
} // namespace openosc::plot
