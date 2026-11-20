#pragma once
#include "lvgl.h"
#include "openosc/settings/theme.hpp"

namespace openosc::ui {
class Elements {
public:
    void bind(ElementId id, lv_obj_t *object) { objects_[unsigned(id)] = object; }
    lv_obj_t *get(ElementId id) const { return objects_[unsigned(id)]; }
    void apply(const ElementStyle *styles);
    void text(ElementId id, const char *value);
    static const lv_font_t *font(unsigned size);
    static int textWidth(const char *text, unsigned size = 12);
    // Every style write redraws the object: an unchanged colour must not keep the display busy.
    static void textColor(lv_obj_t *object, uint32_t color);
    static bool setText(lv_obj_t *label, const char *text); // False when unchanged (nothing invalidated).
    static void overlayPanel(lv_obj_t *object, const Theme &theme);
    static lv_obj_t *label(lv_obj_t *parent, const char *text, int x = 0, int y = 0);

private:
    lv_obj_t *objects_[ElementCount]{};
    char iconPath_[70]{};
};
} // namespace openosc::ui
