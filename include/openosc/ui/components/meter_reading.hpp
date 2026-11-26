#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/components/meter_style.hpp"

namespace openosc::ui {
// The DMM main reading with a sign column: the column is as wide as the minus of the font in use
// (no assumption that a space or digit matches it), left empty for an unsigned text, so a reading
// and its negative put every digit at the same pixels. The font is the largest of the theme's
// size, 32, 24, 14 and 12 in which the sign column and the magnitude fit the label together.
class MeterReading {
public:
    static void show(lv_obj_t *label, const char *text, unsigned size) { show(label, text, font(text, available(label), size)); }
    // With a font chosen by the caller (several readings sharing one font and baseline).
    static void show(lv_obj_t *label, const char *text, const lv_font_t *font, bool reserveSign = true);
    // The largest font up to `size` in which the sign column and the magnitude fit `width`.
    static const lv_font_t *font(const char *text, int width, unsigned size);
    // The same from a reading face's fonts (largest first): the first that fits, else the last.
    static const lv_font_t *font(const char *text, int width, const ReadingFonts &fonts, bool reserveSign = true);
    // The sign column and the magnitude fit `width` in `font`.
    static bool fits(const char *text, int width, const lv_font_t *font);
    // Right-aligned digits keep their sign column; a centred overload uses the entire width.
    static void showDigital(lv_obj_t *label, const char *text, const ReadingFonts &fonts, lv_text_align_t alignment);
    static int available(lv_obj_t *label);
    // The reading and its unit label in `box` (parent coordinates): the largest face font in which
    // the sign column, the reading, `gap` and the unit fit the box's width; the digits, gap and
    // unit centred, the sign column left of them (so x and -x do not move), the whole clamped to
    // the box; the unit on the reading's baseline. Returns the centred width (digits to unit).
    static int showCentred(lv_obj_t *reading, lv_obj_t *unit, const char *text, const ReadingFonts &fonts,
                           const lv_area_t &box, int gap);
    // The width the label's text takes with its sign column (pad_left included).
    static int contentWidth(lv_obj_t *label);
    static void release(lv_obj_t *label); // Another screen's use of the label: no sign column.
    // A minus before a number; the "----.--" placeholder is not signed.
    static bool negative(const char *text) { return text[0] == '-' && ((text[1] >= '0' && text[1] <= '9') || text[1] == '.'); }
};
} // namespace openosc::ui
