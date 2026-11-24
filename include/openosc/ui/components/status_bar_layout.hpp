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
    static constexpr int PillHeight = 17; // 4 px above and below the capitals, not 4/6.
    static constexpr Column BatteryIcon{Width - Margin - 15, 15}, BatteryText{Width - Margin - 32, 32};
    static constexpr int StatusX = Margin;
    // Record overview: the displayed capture's budget and the map of its actual record.
    static constexpr Column Overview{BatteryText.x - Gap - 62, 62};
    static constexpr Column OverviewCaption{Overview.x, 32}; // The widest total ("48.8K", "1023") whole.
    static constexpr Column OverviewMap{OverviewCaption.right() + Tight, Overview.right() - OverviewCaption.right() - Tight};
    static constexpr int IconGap = 3;
    static constexpr Column Generator{Overview.x - IconGap - 9, 9};
    static constexpr Column Usb{Generator.x - IconGap - 9, 9};
    static constexpr Column Fps{Usb.x - Gap - 54, 54}; // The top bar counter, right-aligned: "12.5MS/s" (52.9 px).
    // Trigger edge glyph, channel tag and level ("1 -12.3mV"): one fixed place for every state up to
    // "SGL WAIT"; a wider state hides it.
    static constexpr int EdgeWidth = 8, LevelWidth = 58, TriggerWidth = EdgeWidth + Tight + LevelWidth;
    static constexpr Column Trigger{Fps.x - Gap - TriggerWidth, TriggerWidth};
    static constexpr Column edge(Column trigger) { return {trigger.x, EdgeWidth}; }
    static constexpr Column level(Column trigger) { return {trigger.x + EdgeWidth + Tight, LevelWidth}; }
    // The run profile glyph (Settings > Display > Clock indicator, ProfileGlyph 5 x 7, GitHub #47): just
    // left of the battery, two pixels apart, centred on the battery's own drawing. Battery as Icon: the
    // glyph 296..300 inside the battery's reserved text column, every other place as without it.
    // Battery as Volts/Percent: the glyph 279..283, and the counter, USB, GEN and the record overview
    // closed up to one pixel apart (USB 195..203, GEN 205..213, overview 215..276, 62 px as always) with
    // two pixels before the glyph. Off or outside the Scope: the places above. Only the setting, the
    // mode or the battery display moves them; readings, states and the profile never do.
    static constexpr int ClockWidth = 5, ClockHeight = 7, ClockGap = 2, DenseGap = 1;
    static constexpr int ClockYIcon = 6, ClockYText = 7; // Centred on the drawn battery (tests/native/scope/clock_indicator.cpp).
    static constexpr Column ClockBesideIcon{BatteryIcon.x - ClockGap - ClockWidth, ClockWidth};
    static constexpr Column ClockBesideText{BatteryText.x - ClockGap - ClockWidth, ClockWidth};
    static constexpr Column OverviewDense{ClockBesideText.x - ClockGap - Overview.width, Overview.width};
    static constexpr Column GeneratorDense{OverviewDense.x - DenseGap - Generator.width, Generator.width};
    static constexpr Column UsbDense{GeneratorDense.x - DenseGap - Usb.width, Usb.width};
    static constexpr Column clock(bool textBattery) { return textBattery ? ClockBesideText : ClockBesideIcon; }
    static constexpr int clockY(bool textBattery) { return textBattery ? ClockYText : ClockYIcon; }
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
static_assert(L::ClockBesideIcon.x == 296 && L::ClockBesideIcon.right() + L::ClockGap == L::BatteryIcon.x, "Glyph 296..300, 2 px before the icon");
static_assert(L::ClockBesideIcon.x > L::Overview.right(), "Beside the icon the overview keeps its place");
static_assert(L::ClockBesideText.x == 279 && L::ClockBesideText.right() + L::ClockGap == L::BatteryText.x, "Glyph 279..283, 2 px before the text");
static_assert(L::OverviewDense.x == 215 && L::OverviewDense.width == 62 && L::OverviewDense.right() + L::ClockGap == L::ClockBesideText.x,
              "The overview whole, 215..276, 2 px before the glyph");
static_assert(L::GeneratorDense.x == 205 && L::UsbDense.x == 195 && L::UsbDense.x - L::DenseGap == L::Fps.right(),
              "USB 195..203, GEN 205..213, 1 px from the counter and each other");
static_assert(L::StatusX + L::MaximumStatus <= L::Fps.right(), "The widest pill ends before the counter's right edge");
static_assert(L::ClockYIcon >= 0 && L::ClockYText >= 0 && L::ClockYIcon + L::ClockHeight <= L::Height &&
              L::ClockYText + L::ClockHeight <= L::Height, "The glyph inside the bar");
} // namespace status_bar_checks
} // namespace openosc::ui
