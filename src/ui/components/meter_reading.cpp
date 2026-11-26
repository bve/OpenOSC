#include "openosc/ui/components/meter_reading.hpp"
#include <algorithm>
#include <cstring>
#include <initializer_list>

namespace openosc::ui {
namespace {
// LVGL's own measure: UTF-8 aware (µ, Ω are two bytes, one glyph each).
int width(const lv_font_t *font, const char *text) {
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}
int sign(const lv_font_t *font, const char *magnitude) { return lv_font_get_glyph_width(font, '-', uint8_t(*magnitude)); }
const char *magnitude(const char *text) { return MeterReading::negative(text) ? text + 1 : text; }
} // namespace
int MeterReading::available(lv_obj_t *label) {
    // The theme's width: the coordinates lag until the next layout (right after an activation).
    const int32_t style = lv_obj_get_style_width(label, LV_PART_MAIN);
    return LV_COORD_IS_PX(style) ? style : lv_obj_get_width(label);
}
const lv_font_t *MeterReading::font(const char *text, int available, unsigned size) {
    ReadingFonts fonts;
    for (unsigned candidate : {size, 32U, 24U, 14U, 12U})
        if (candidate <= size) fonts.fonts[fonts.count++] = Elements::font(candidate);
    return font(text, available, fonts);
}
bool MeterReading::fits(const char *text, int available, const lv_font_t *font) {
    const char *digits = magnitude(text);
    return sign(font, digits) + width(font, digits) <= available;
}
const lv_font_t *MeterReading::font(const char *text, int available, const ReadingFonts &fonts, bool reserveSign) {
    for (unsigned i = 0; i < fonts.count; ++i) {
        const auto *face = fonts.fonts[i];
        if (reserveSign ? fits(text, available, face) : width(face, text) <= available) return face;
    }
    return fonts.fonts[fonts.count - 1];
}
void MeterReading::show(lv_obj_t *label, const char *text, const lv_font_t *font, bool reserveSign) {
    if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != font) lv_obj_set_style_text_font(label, font, 0);
    const int padding = reserveSign && !negative(text) ? sign(font, text) : 0;
    if (lv_obj_get_style_pad_left(label, LV_PART_MAIN) != padding) lv_obj_set_style_pad_left(label, padding, 0);
    if (std::strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
}
void MeterReading::showDigital(lv_obj_t *label, const char *text, const ReadingFonts &fonts, lv_text_align_t alignment) {
    if (lv_obj_get_style_text_align(label, LV_PART_MAIN) != alignment) lv_obj_set_style_text_align(label, alignment, 0);
    const bool reserveSign = alignment != LV_TEXT_ALIGN_CENTER;
    show(label, text, font(text, available(label), fonts, reserveSign), reserveSign);
}
int MeterReading::contentWidth(lv_obj_t *label) {
    return lv_obj_get_style_pad_left(label, LV_PART_MAIN) + width(lv_obj_get_style_text_font(label, LV_PART_MAIN), lv_label_get_text(label));
}
int MeterReading::showCentred(lv_obj_t *reading, lv_obj_t *unit, const char *text, const ReadingFonts &fonts,
                              const lv_area_t &box, int gap) {
    const auto *unitFont = lv_obj_get_style_text_font(unit, LV_PART_MAIN);
    const int unitWidth = width(unitFont, lv_label_get_text(unit)), space = unitWidth ? gap : 0;
    const int boxWidth = lv_area_get_width(&box);
    const auto *font = MeterReading::font(text, boxWidth - space - unitWidth, fonts);
    show(reading, text, font);
    // The digits and the unit are centred; the sign column sits left of them (empty for a
    // positive reading), so x and -x put digits and unit at the same pixels. The whole, sign
    // column included, stays inside the box.
    const int readingWidth = contentWidth(reading), slot = sign(font, magnitude(text));
    const int visible = readingWidth - slot + space + unitWidth, whole = readingWidth + space + unitWidth;
    const int left = std::clamp(box.x1 + (boxWidth - visible) / 2 - slot, box.x1, std::max(box.x1, box.x1 + boxWidth - whole));
    lv_obj_set_pos(reading, left, box.y1);
    lv_obj_set_size(reading, readingWidth, lv_area_get_height(&box));
    // The unit's baseline on the reading's.
    const int baseline = box.y1 + font->line_height - font->base_line;
    lv_obj_set_pos(unit, left + readingWidth + space, baseline - (unitFont->line_height - unitFont->base_line));
    lv_obj_set_size(unit, std::max(1, unitWidth), unitFont->line_height);
    return visible;
}
void MeterReading::release(lv_obj_t *label) {
    if (lv_obj_get_style_pad_left(label, LV_PART_MAIN)) lv_obj_set_style_pad_left(label, 0, 0);
}
} // namespace openosc::ui
