#include "openosc/ui/components/power_hold_overlay.hpp"

namespace openosc::ui {
void PowerHoldOverlay::create() {
    const Theme &theme = *theme_;
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 200, 56);
    lv_obj_align(root_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme.panel), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(root_, lv_color_hex(theme.accent), 0);
    lv_obj_set_style_border_width(root_, 1, 0);
    lv_obj_set_style_radius(root_, 3, 0);
    lv_obj_set_scrollable(root_, false);
    title_ = Elements::label(root_, "", 0, 8);
    lv_obj_set_width(title_, 200);
    lv_obj_set_style_text_align(title_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title_, lv_color_hex(theme.foreground), 0);
    bar_ = lv_bar_create(root_);
    lv_obj_set_size(bar_, 176, 10);
    lv_obj_set_pos(bar_, 12, 34);
    lv_bar_set_range(bar_, 0, 100);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(theme.grid), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, lv_color_hex(theme.accent), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_hidden(root_, true);
}
void PowerHoldOverlay::show(int percent, const char *title) {
    if (!theme_) return;
    if (!root_) { if (percent < 0) return; create(); }
    if (percent >= 0) Elements::setText(title_, title);
    if (percent == shown_) return;
    shown_ = percent;
    lv_obj_set_hidden(root_, percent < 0);
    if (percent >= 0) lv_bar_set_value(bar_, percent, LV_ANIM_OFF);
}
} // namespace openosc::ui
