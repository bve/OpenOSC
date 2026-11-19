#pragma once
#include "lvgl.h"
#include "openosc/settings/theme.hpp"
#include "openosc/settings/meter_appearance.hpp"
#include <cstdint>

namespace openosc::ui {
// The DMM's own colours: the body below the parameter cards (the status bar and cards keep the
// global theme). Dark follows the theme, so the default DMM looks as before.
struct MeterPalette {
    uint32_t background, ink, muted, accent, line, needle, alert;
};
// The reading's typeface: the largest of its sizes (up to the theme's size) in which the reading
// fits, then the shared smaller numeric fonts.
struct ReadingFonts {
    static constexpr unsigned Capacity = 10;
    const lv_font_t *fonts[Capacity]{};
    unsigned count = 0;
};
// The DMM's look with every element resolved: the base palette and face, each role's colour (its
// override, else the base colour it had before overrides existed), the unit's font and the dial's
// line widths. Computed when the style changes, never per frame.
struct MeterLook {
    MeterPalette palette{};      // The base: muted (no live reading), alert, line (separators, unlit).
    uint32_t colors[MeterAppearance::Roles]{};
    uint8_t fonts[MeterAppearance::Roles]{}; // Each text role's typeface (MeterAppearanceCatalog::TextFont).
    unsigned face = 0;           // The reading's face (MeterStyles::Face): its override, else the legacy face.
    uint8_t arcWidth = 2, tickWidth = 1, needleWidth = 2;
    uint32_t color(MeterRole role) const { return colors[unsigned(role)]; }
    // A text role's font at `size` (12, 14, 24 ...): IBM Plex Sans or Noto Sans Mono of that size.
    const lv_font_t *text(MeterRole role, unsigned size) const;
    bool operator==(const MeterLook &b) const;
};
class MeterStyles {
public:
    enum Face : uint8_t { Ibm, Mono, Segment, Faces };
    enum Scheme : uint8_t { Dark, Amber, Paper, Schemes };
    static const char *faceName(unsigned face);     // "IBM", "Mono", "Segment"
    static const char *paletteName(unsigned scheme); // "Dark", "Amber", "Paper"
    static MeterPalette palette(unsigned scheme, const Theme &theme);
    static ReadingFonts reading(unsigned face, unsigned size);
    // A text font of `family` (TextFont) at `size`: IBM is Elements::font(size); Mono has 12, 14, 24,
    // then the numeric 32 and 40 (the same glyph set: ASCII, °, ±, µ, Ω, Δ, ×, ÷, minus).
    static const lv_font_t *text(unsigned family, unsigned size);
    // The resolved look of a palette, a face and the overrides over the theme (`parameters`: the
    // DMM cards' highlight, the theme's Parameters colour).
    static MeterLook look(unsigned scheme, unsigned face, const MeterAppearance &appearance, const Theme &theme, uint32_t parameters);
};
} // namespace openosc::ui
