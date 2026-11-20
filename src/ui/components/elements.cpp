#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/fonts.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::ui {
int Elements::textWidth(const char *text, unsigned size) {
    int width = 0;
    for (const char *p = text; *p; ++p) width += lv_font_get_glyph_width(font(size), uint8_t(*p), uint8_t(p[1]));
    return width;
}
void Elements::textColor(lv_obj_t *object, uint32_t color) {
    const auto value = lv_color_hex(color);
    if (!lv_color_eq(lv_obj_get_style_text_color(object, LV_PART_MAIN), value)) lv_obj_set_style_text_color(object, value, 0);
}
bool Elements::setText(lv_obj_t *label, const char *text) {
    if (!std::strcmp(lv_label_get_text(label), text)) return false;
    lv_label_set_text(label, text);
    return true;
}
void Elements::overlayPanel(lv_obj_t *object, const Theme &theme) {
    lv_obj_set_style_bg_color(object, lv_color_hex(theme.panel), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(object, lv_color_hex(theme.accent), 0);
    lv_obj_set_style_border_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 1, 0);
    lv_obj_set_style_radius(object, 3, 0);
}
const lv_font_t *Elements::font(unsigned size) {
    switch (size) {
    case 11: return &openosc_ui_11c; // Condensed: only where IBM Plex Sans 12 has no room (ScopeFooter).
    case 14: return &openosc_ui_14;
    case 24: return &openosc_ui_24;
    case 32: return &openosc_numeric_32;
    case 40: return &openosc_numeric_40;
    case 56: return &openosc_numeric_56;
    case 64: return &openosc_ibm_64;
    default: return &openosc_ui_12;
    }
}
lv_obj_t *Elements::label(lv_obj_t *parent, const char *value, int x, int y) {
    auto *object = lv_label_create(parent);
    lv_label_set_text(object, value);
    lv_label_set_long_mode(object, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(object, font(12), 0);
    lv_obj_set_style_text_letter_space(object, 0, 0);
    lv_obj_set_style_text_line_space(object, 0, 0);
    lv_obj_set_pos(object, x, y);
    return object;
}
void Elements::text(ElementId id, const char *value) {
    auto *object = get(id);
    if (std::strcmp(lv_label_get_text(object), value))
        lv_label_set_text(object, value);
}
void Elements::apply(const ElementStyle *styles) {
    for (unsigned i = 0; i < ElementCount; ++i) {
        auto *object = objects_[i];
        const auto &e = styles[i];
        lv_obj_set_pos(object, e.x, e.y);
        lv_obj_set_size(object, e.width, e.height);
        lv_obj_set_style_text_color(object, lv_color_hex(e.color), 0);
        lv_obj_set_style_text_font(object, font(e.font), 0);
        lv_obj_set_hidden(object, !e.visible);
    }
    for (unsigned z = 0; z <= ElementZOrderMax; ++z)
        for (unsigned i = 0; i < ElementCount; ++i)
            if (styles[i].zOrder == z)
                lv_obj_move_foreground(objects_[i]);
    const auto &icon = styles[unsigned(ElementId::Icon)];
    if (icon.visible && icon.icon[0]) {
        std::snprintf(iconPath_, sizeof iconPath_, "S:%s", icon.icon);
        lv_image_set_src(get(ElementId::Icon), iconPath_);
    }
}
} // namespace openosc::ui
