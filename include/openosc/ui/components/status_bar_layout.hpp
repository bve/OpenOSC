#pragma once

namespace openosc::ui {
struct StatusColumn {
    int x, width;
    constexpr int right() const { return x + width; } // First column past it.
};
// Columns of the persistent 20-pixel status bar. From the left: the state pill, then the trigger
// edge and level and the FPS readout. From the right: battery at the panel's edge, the record
// overview, then the GEN and USB icons. Reserve the widest battery mode so changing between
// icon, volts and percent never moves the other indicators. No pill reaches this fixed right
// side. The trigger takes
// the first of two fixed places the pill leaves free, FPS shows where nothing else is drawn.
// Widths are IBM Plex Sans 12 extents of the widest texts: "4.12V", "2 -88.8mV", "125 fps"
// (LVGL may refresh every 8 ms), "48.8K"; tests/native/scope/status_bar.cpp renders them all.
struct StatusBarLayout {
    using Column = StatusColumn;
    static constexpr int Width = 320, Height = 20, Margin = 2, Gap = 3, Tight = 2;
    static constexpr int PillPadding = 4; // Each side of the state text.
    static constexpr int PillRadius = 2;  // The RUN/HOLD pill's corners; the soft-key cards take the same.
    static constexpr Column BatteryIcon{Width - Margin - 15, 15}, BatteryText{Width - Margin - 32, 32};
    static constexpr int StatusX = Margin;
    // Record overview: the total points of the frame on show, then the map of that record.
    static constexpr Column Overview{BatteryText.x - Gap - 62, 62};
    static constexpr Column OverviewCaption{Overview.x, 32}; // The widest total ("48.8K", "1023") whole.
    static constexpr Column OverviewMap{OverviewCaption.right() + Tight, Overview.right() - OverviewCaption.right() - Tight};
    static constexpr int IconGap = 3;
    static constexpr Column Generator{Overview.x - IconGap - 9, 9};
    static constexpr Column Usb{Generator.x - IconGap - 9, 9};
    static constexpr Column Fps{Usb.x - Gap - 54, 54}; // The top bar counter, right-aligned: "12.5MS/s" (52.9 px).
    // Trigger edge glyph and level ("1 -12.3mV"): one fixed place for every state up to "SGL WAIT";
    // a wider state hides it.
    static constexpr int EdgeWidth = 8, LevelWidth = 58, TriggerWidth = EdgeWidth + Tight + LevelWidth;
    static constexpr Column Trigger{Fps.x - Gap - TriggerWidth, TriggerWidth};
    static constexpr Column edge(Column trigger) { return {trigger.x, EdgeWidth}; }
    static constexpr Column level(Column trigger) { return {trigger.x + EdgeWidth + Tight, LevelWidth}; }
    // The pill is clipped here rather than run into the USB icon.
    static constexpr int MaximumStatus = Usb.x - Gap - StatusX;

    static constexpr Column battery(bool text) { return text ? BatteryText : BatteryIcon; }
    // Whether a pill ending at statusRight leaves the column visible.
    static constexpr bool clear(int statusRight, Column column) { return statusRight + Gap <= column.x; }
    static constexpr bool trigger(int statusRight) { return clear(statusRight, Trigger); }
};
namespace status_bar_checks {
using L = StatusBarLayout;
static_assert(L::BatteryIcon.right() + L::Margin == L::Width && L::BatteryText.right() == L::BatteryIcon.right(), "Battery at the right edge in every mode");
static_assert(L::OverviewMap.width >= 28 && L::Overview.right() + L::Gap == L::BatteryText.x, "Overview clears every battery mode");
static_assert(L::Generator.right() + L::IconGap == L::Overview.x, "GEN reaches the overview");
static_assert(L::Usb.right() + L::IconGap == L::Generator.x, "USB reaches GEN");
static_assert(L::Trigger.right() + L::Gap == L::Fps.x, "The trigger reaches the counter");
static_assert(L::StatusX + 64 + L::Gap <= L::Trigger.x, "\"SGL WAIT\" keeps the trigger");
static_assert(L::MaximumStatus >= 154, "\"CAPTURE CONFIG ERROR\" must stay readable");
} // namespace status_bar_checks
} // namespace openosc::ui
