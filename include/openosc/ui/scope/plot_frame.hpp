#pragma once

namespace openosc::plot {
// The waveform object is the plot plus narrow margins that hold its position markers, so no
// marker covers the signal: left, the CH1/CH2 zero tags; right, the trigger level; top, the
// trigger position. The model projects onto the plot only (its Viewport is the plot's size);
// the theme places the object, so the stock layouts keep a 300-column plot.
struct PlotFrame {
    static constexpr int Left = 11, Right = 7, Top = 8, Bottom = 0;
    static constexpr int PlotX = 12, PlotWidth = 300; // The stock plot columns on the 320-pixel panel.
    static constexpr int ObjectX = PlotX - Left, ObjectWidth = Left + PlotWidth + Right;
    static constexpr int objectY(int plotY) { return plotY - Top; }
    // Never negative: a custom object smaller than its margins has an empty plot.
    static constexpr int plotWidth(int objectWidth) { return objectWidth > Left + Right ? objectWidth - Left - Right : 0; }
    static constexpr int plotHeight(int objectHeight) { return objectHeight > Top + Bottom ? objectHeight - Top - Bottom : 0; }
    // A zero tag: a box with the channel digit, Tag pixels high, and a tip to the plot edge.
    static constexpr int TagWidth = 9, TagHeight = 11, TagGap = 1, TipWidth = Left - TagWidth;
    static constexpr int ArrowHeight = 4; // A zero above/below the plot: an arrow beyond its tag.
};
static_assert(PlotFrame::ObjectX >= 0 && PlotFrame::ObjectX + PlotFrame::ObjectWidth <= 320, "Stock object fits the panel");
static_assert(PlotFrame::TipWidth >= 2, "Room for the zero tag's tip");

// Vertical places of the two zero tags in the left margin (plot coordinates). Each tag is centred
// on its channel's zero clamped to the plot; a zero above or below the plot puts its tag at that
// end, leaving room for its arrow; two tags closer than a tag apart move away from each other
// around their middle and stay inside the margin's height; the tips still point at the exact zeros.
struct ZeroTags {
    static void place(const int (&zero)[2], const bool (&shown)[2], int plotHeight, int (&centre)[2]);
    static int mathCentre(int zero, int plotHeight, const bool (&shown)[2], const int (&centre)[2]);
};
} // namespace openosc::plot
