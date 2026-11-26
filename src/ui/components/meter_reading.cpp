#include "openosc/ui/components/meter_reading.hpp"
#include "openosc/ui/format/meter.hpp"
#include <algorithm>
#include <cstring>

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
int MeterReading::contentWidth(lv_obj_t *label) {
    return lv_obj_get_style_pad_left(label, LV_PART_MAIN) + width(lv_obj_get_style_text_font(label, LV_PART_MAIN), lv_label_get_text(label));
}
int MeterReading::unitArea(const lv_font_t *font, const char *current) {
    int widest = current ? width(font, current) : 0;
    for (const char *unit : MeterText::ReadingUnits) widest = std::max(widest, width(font, unit));
    return widest;
}
void MeterReading::showRow(lv_obj_t *reading, lv_obj_t *unit, const char *text, const ReadingFonts &fonts,
                           const lv_area_t &box, int gap, bool centred, bool overload) {
    const auto *unitFont = lv_obj_get_style_text_font(unit, LV_PART_MAIN);
    const int boxWidth = lv_area_get_width(&box);
    // Every font on the largest one's baseline: a fallback size sits on the same line.
    const int baseline = box.y1 + fonts.fonts[0]->line_height - fonts.fonts[0]->base_line;
    const auto place = [baseline](lv_obj_t *label, const lv_font_t *font, int x, int w) {
        lv_obj_set_pos(label, x, baseline - (font->line_height - font->base_line));
        lv_obj_set_size(label, std::max(1, w), font->line_height);
    };
    const auto align = [](lv_obj_t *label, lv_text_align_t value) {
        if (lv_obj_get_style_text_align(label, LV_PART_MAIN) != value) lv_obj_set_style_text_align(label, value, 0);
    };
    align(unit, LV_TEXT_ALIGN_LEFT);
    if (overload) { // OL across the whole box, no unit beside it.
        const auto *font = MeterReading::font(text, boxWidth, fonts, false);
        align(reading, LV_TEXT_ALIGN_CENTER);
        show(reading, text, font, false);
        place(reading, font, box.x1, boxWidth);
        place(unit, unitFont, box.x2, 1);
        return;
    }
    const char *suffix = lv_label_get_text(unit);
    const int area = lv_obj_is_hidden(unit) ? 0 : centred ? width(unitFont, suffix) : unitArea(unitFont, suffix);
    const int space = area ? gap : 0;
    const auto *font = MeterReading::font(text, boxWidth - space - area, fonts);
    align(reading, LV_TEXT_ALIGN_RIGHT);
    show(reading, text, font);
    const int slot = sign(font, magnitude(text));
    const int digits = contentWidth(reading), whole = digits + space + area;
    const int left = centred ? box.x1 + (boxWidth - (whole - slot)) / 2 - slot : box.x2 + 1 - whole;
    const int x = std::clamp<int>(left, box.x1, std::max<int>(box.x1, box.x2 + 1 - whole));
    place(reading, font, x, digits);
    place(unit, unitFont, std::min<int>(x + digits + space, box.x2), area);
}
void MeterReading::release(lv_obj_t *label) {
    if (lv_obj_get_style_pad_left(label, LV_PART_MAIN)) lv_obj_set_style_pad_left(label, 0, 0);
}
} // namespace openosc::ui
