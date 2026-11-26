#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/components/meter_style.hpp"

namespace openosc::ui {
// The DMM main reading with a sign column: the column is as wide as the minus of the font in use
// (no assumption that a space or digit matches it), left empty for an unsigned text, so a reading
// and its negative put every digit at the same pixels.
class MeterReading {
public:
    // With a font chosen by the caller (several readings sharing one font and baseline).
    static void show(lv_obj_t *label, const char *text, const lv_font_t *font, bool reserveSign = true);
    // From a reading face's fonts (largest first): the first in which the text fits, else the last.
    static const lv_font_t *font(const char *text, int width, const ReadingFonts &fonts, bool reserveSign = true);
    // The sign column and the magnitude fit `width` in `font`.
    static bool fits(const char *text, int width, const lv_font_t *font);
    // The unit area beside the digits: the widest unit a reading can show (MeterText::ReadingUnits)
    // or `current`, in `font`, so a change of unit or range never moves the digits.
    static int unitArea(const lv_font_t *font, const char *current = nullptr);
    // The reading row in `box` (parent coordinates), the digital and the analog face alike: the
    // sign column and digits, followed by the unit on their baseline. Digital reserves a fixed
    // unit area at the right edge; analog centres the printed digits and unit. Use the largest font
    // that fits the current reading and keep its baseline even when the size changes. For overload,
    // centre OL across the whole box; the caller clears the unit's text.
    static void showRow(lv_obj_t *reading, lv_obj_t *unit, const char *text, const ReadingFonts &fonts,
                        const lv_area_t &box, int gap, bool centred, bool overload);
    // The width the label's text takes with its sign column (pad_left included).
    static int contentWidth(lv_obj_t *label);
    static void release(lv_obj_t *label); // Another screen's use of the label: no sign column.
    // A minus before a number; the "----.--" placeholder is not signed.
    static bool negative(const char *text) { return text[0] == '-' && ((text[1] >= '0' && text[1] <= '9') || text[1] == '.'); }
};
} // namespace openosc::ui
