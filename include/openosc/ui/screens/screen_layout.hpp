#pragma once
#include "openosc/settings/screen_arrangement.hpp"
#include "openosc/ui/components/scope_footer_layout.hpp"
#include "openosc/ui/components/status_bar_layout.hpp"
#include "openosc/ui/scope/plot_frame.hpp"

namespace openosc::ui {
// Where each block of a screen stands: the blocks of an order (screen_arrangement.hpp) stacked top
// to bottom under the status bar, every block at its own fixed height (nothing is scaled to make
// room). The rows left over become small equal gaps between the blocks, so the first block starts
// right under the status bar and the last one ends at the bottom edge; an absent block (hidden:
// Statistics Off) takes no rows and leaves no hole. Pure geometry, no LVGL objects.
struct BlockSpan {
    int y = 0, height = 0;
    bool shown = false;
    constexpr int bottom() const { return y + height - 1; }
};
// The rows of the protocol decoder's table (docs/SCOPE_PROTOCOL_DECODERS_PLAN.md §4.2, §4.3), from
// its top, each one openosc_mono_12 line (13 px): the info row (state, warnings, rec/s, age,
// coverage) when the table has one, the header, then `events` event rows (or Detail lines).
struct DecoderTableRows {
    static constexpr int Row = 13;
    bool info = false;
    unsigned events = 0;
    constexpr int header() const { return info ? Row : 0; }                      // Its offset.
    constexpr int event(unsigned row) const { return header() + Row * int(row + 1); } // Row `row`'s offset.
    constexpr int height() const { return event(events); }
    constexpr bool operator==(const DecoderTableRows &o) const { return info == o.info && events == o.events; }
    constexpr bool operator!=(const DecoderTableRows &o) const { return !(*this == o); }
};
class ScreenLayout {
public:
    static constexpr int Top = StatusBarLayout::Height, Bottom = 240, Body = Bottom - Top; // y 20..239.
    // The soft keys: the same bar wherever Buttons stands, four equal 76 px zones 4 px apart.
    static constexpr int ButtonsX = 2, ButtonsWidth = 316, ButtonsHeight = 32, ButtonGap = 4;
    static constexpr int ButtonWidth = (ButtonsWidth - 3 * ButtonGap) / 4;
    // A menu or panel over a screen: the keys' bar at the bottom, right over the physical keys, in
    // every screen order; the menu between the status bar and it (y 22..203).
    static constexpr int MenuKeysY = Bottom - ButtonsHeight, MenuTop = Top + 2, MenuGap = 4;
    static constexpr int MenuHeight = MenuKeysY - MenuGap - MenuTop;
    // Scope: the readouts row (ScopeFooter, as high as the status bar) and the waveform object:
    // its marker margin above the 156-row plot (PlotFrame::Top), nothing below it.
    static constexpr int Readouts = ScopeFooterLayout::Height, PlotHeight = 156, Waveform = plot::PlotFrame::Top + PlotHeight;
    // The DMM's blocks. Digital: the reading block is the function sign row (sign, jacks, unit
    // badge) over the 64 px digits; the measurements block is the meta row, MIN/MAX/AVG and the bar
    // (without the statistics only the meta row and the bar). Analog: the dial block is the sign
    // and meta row over the 92 px dial; measurements is MIN/MAX/AVG on one row; reading is the 64 px
    // digits with their unit.
    static constexpr int SignRow = 27, Digits = 64, DigitalReading = SignRow + Digits;
    static constexpr int DigitalMeasurements = 89, DigitalMeterNoStatistics = 50, DigitalMeterMetaOnly = 24;
    // Primary without bar graph and statistics: the reading takes the freed rows, in up to 80 px digits.
    static constexpr int LargeDigits = 80, DigitalReadingLarge = SignRow + LargeDigits;
    static constexpr int MetaRow = 14, DialOffset = 9, DialHeight = 92, AnalogDial = DialOffset + DialHeight;
    static constexpr int AnalogMeasurements = 14, AnalogReading = 62;
    // The generator: the frequency block is its title row, the digits with their unit and the
    // Applied row (offsets from the block's top, from the default theme's 66..175); the preview
    // keeps its curve and its level line (GeneratorPreview needs 60 for both).
    static constexpr int GeneratorTitle = 0, GeneratorDigits = 18, GeneratorUnit = 37, GeneratorApplied = 77;
    static constexpr int GeneratorFrequency = 110, GeneratorPreview = 68;
    // The protocol decoder's own Scope layout (plan §4.2), not an order: the work area from the
    // status bar down, the keys at MenuKeysY. Decoder only: the table's info row, header and 12 rows.
    // Split: the waveform's marker margin and a 92-row plot over the header and 5 rows; no info row
    // there (the plan's 5 rows in the same 78 px; the status bar keeps RUN/HOLD). Wave + annotations
    // keeps the user's order: its strip covers the plot's bottom rows.
    static constexpr int DecoderRow = DecoderTableRows::Row, DecoderRows = 12, SplitRows = 5;
    static constexpr int SplitWaveform = plot::PlotFrame::Top + 92, AnnotationStrip = 12;
    static constexpr DecoderTableRows DecoderOnlyTable{true, DecoderRows}, SplitTable{false, SplitRows};
    // Decoder only's height for `rows` event rows (its info row and header included).
    static constexpr int decoderTable(int rows) { return DecoderTableRows{true, unsigned(rows)}.height(); }
    template<class Block>
    static void stack(const BlockOrder<Block> &order, const int (&heights)[BlockOrder<Block>::Size],
                      BlockSpan (&spans)[BlockOrder<Block>::Size]) {
        constexpr unsigned N = BlockOrder<Block>::Size;
        unsigned shown = 0;
        int used = 0;
        for (unsigned i = 0; i < N; ++i) {
            spans[i] = {};
            if (heights[i] > 0) { ++shown; used += heights[i]; }
        }
        const int free = Body - used, gaps = shown > 1 ? int(shown) - 1 : 1;
        int y = Top, gap = 0;
        for (unsigned slot = 0; slot < N; ++slot) {
            const unsigned block = unsigned(order.at(slot));
            if (heights[block] <= 0) continue;
            y += gap > 0 ? free / gaps + (gap <= free % gaps ? 1 : 0) : 0;
            spans[block] = {y, heights[block], true};
            y += heights[block];
            ++gap;
        }
    }
    // Scope: [Measurements, Waveform, Buttons] spans by ScopeBlock; `readouts` false: no readouts row
    // (it takes no rows).
    static void scope(const ScopeArrangement &order, bool readouts, BlockSpan (&spans)[ScopeArrangement::Size]) {
        const int heights[] = {readouts ? Readouts : 0, Waveform, ButtonsHeight};
        stack(order, heights, spans);
    }
    // The meta and statistics rows sit on the keys; the reading takes every spare line.
    static void digital(const DigitalArrangement &order, bool statistics, bool bar, BlockSpan (&spans)[DigitalArrangement::Size]) {
        const int meter = statistics ? DigitalMeasurements : bar ? DigitalMeterNoStatistics : DigitalMeterMetaOnly;
        const int heights[] = {Body - meter - ButtonsHeight, meter, ButtonsHeight};
        stack(order, heights, spans);
    }
    // The digit size a reading block of this height holds: 80, 72 or 64 px.
    static constexpr int digitsFor(int reading) {
        return reading >= SignRow + LargeDigits ? LargeDigits : reading >= SignRow + 72 ? 72 : Digits;
    }
    static void generator(const GeneratorArrangement &order, BlockSpan (&spans)[GeneratorArrangement::Size]) {
        const int heights[] = {GeneratorFrequency, GeneratorPreview, ButtonsHeight};
        stack(order, heights, spans);
    }
    static void analog(const AnalogArrangement &order, bool statistics, BlockSpan (&spans)[AnalogArrangement::Size]) {
        const int heights[] = {AnalogDial, statistics ? AnalogMeasurements : 0, AnalogReading, ButtonsHeight};
        stack(order, heights, spans);
    }
};
static_assert(ScreenLayout::ButtonWidth == 76, "four equal 76 px zones");
static_assert(ScreenLayout::Top + ScreenLayout::DecoderOnlyTable.height() <= ScreenLayout::MenuKeysY, "Decoder only: info, header and 12 rows above the keys");
static_assert(ScreenLayout::Top + ScreenLayout::SplitWaveform + ScreenLayout::SplitTable.height() <= ScreenLayout::MenuKeysY,
              "Split: the plot, the header and 5 rows above the keys");
static_assert(ScreenLayout::MenuKeysY == 208 && ScreenLayout::MenuTop + ScreenLayout::MenuHeight <= 204, "A menu ends above its keys");
static_assert(ScreenLayout::Readouts + ScreenLayout::Waveform + ScreenLayout::ButtonsHeight <= ScreenLayout::Body, "the Scope fits");
static_assert(ScreenLayout::GeneratorFrequency + ScreenLayout::GeneratorPreview + ScreenLayout::ButtonsHeight <= ScreenLayout::Body, "the generator fits");
static_assert(ScreenLayout::DigitalReading + ScreenLayout::DigitalMeasurements + ScreenLayout::ButtonsHeight <= ScreenLayout::Body, "digital fits");
static_assert(ScreenLayout::DigitalReadingLarge + ScreenLayout::DigitalMeterMetaOnly + ScreenLayout::ButtonsHeight <= ScreenLayout::Body,
              "the large reading fits");
static_assert(ScreenLayout::AnalogDial + ScreenLayout::AnalogMeasurements + ScreenLayout::AnalogReading + ScreenLayout::ButtonsHeight <=
              ScreenLayout::Body, "analog fits");
} // namespace openosc::ui
