#include "openosc/ui/components/menu_row.hpp"
#include <initializer_list>
#include <cstring>

namespace openosc::ui {
MenuRow::MenuRow(lv_obj_t *parent, int y, int height, const char *name, const char *value,
                 bool selected, const Theme &theme, uint32_t accent) : accent_(accent) {
    update(parent,y,height,name,value,selected,theme,accent);
}
void MenuRow::update(lv_obj_t *parent, int y, int height, const char *name, const char *value,
                     bool selected, const Theme &theme, uint32_t accent, int x, int width) {
    accent_=accent;
    if (!root_) {
        root_=lv_button_create(parent);
        lv_obj_remove_style_all(root_);
        lv_obj_set_scrollable(root_,false);
        name_=Elements::label(root_,"");
        value_=Elements::label(root_,"");
    }
    lv_obj_set_hidden(root_,false);
    if (toggle_) lv_obj_set_hidden(toggle_,true);
    if (slider_) lv_obj_set_hidden(slider_,true);
    nameSwatch_.hide(); valueSwatch_.hide();
    lv_obj_set_style_pad_left(name_, 0, 0);
    ink_=theme.foreground; height_=height; width_=width;
    lv_obj_set_pos(root_, x, y);
    lv_obj_set_size(root_, width, height);
    lv_obj_set_scrollable(root_, false);
    lv_obj_set_style_radius(root_, 2, 0);
    lv_obj_set_style_border_width(root_, Border, 0);
    lv_obj_set_style_border_color(root_, lv_color_hex(selected ? accent : theme.panel), 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(selected ? theme.grid : theme.panel), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(root_, Padding, 0);
    lv_obj_set_style_pad_ver(root_, 0, 0);
    lv_obj_set_style_pad_column(root_, 6, 0);
    static const int32_t columns[] = {LV_GRID_FR(1), 105, LV_GRID_TEMPLATE_LAST};
    static const int32_t wideColumns[] = {LV_GRID_FR(1), 150, LV_GRID_TEMPLATE_LAST};
    static const int32_t rows[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    int valueWidth=0;
    if (value) for (const char *p=value; *p; ++p)
        valueWidth+=lv_font_get_glyph_width(Elements::font(14),uint8_t(*p),uint8_t(p[1]));
    valueWidth_=valueWidth;
    lv_obj_set_grid_dsc_array(root_, valueWidth>105 ? wideColumns : columns, rows);
    if (std::strcmp(lv_label_get_text(name_),name)) lv_label_set_text(name_,name);
    lv_obj_set_height(name_, 18);
    lv_obj_set_style_text_font(name_, Elements::font(14), 0);
    lv_obj_set_style_text_color(name_, lv_color_hex(theme.foreground), 0);
    lv_obj_set_grid_cell(name_, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_label_set_long_mode(name_, LV_LABEL_LONG_DOT);
    if (std::strcmp(lv_label_get_text(value_),value ? value : "")) lv_label_set_text(value_,value ? value : "");
    lv_obj_set_hidden(value_,false);
    lv_obj_set_height(value_, 18);
    lv_obj_set_style_text_font(value_, Elements::font(14), 0);
    lv_obj_set_style_text_align(value_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(value_, lv_color_hex(selected ? accent : theme.foreground), 0);
    lv_obj_set_grid_cell(value_, LV_GRID_ALIGN_STRETCH, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_label_set_long_mode(value_, LV_LABEL_LONG_DOT);
    if (!value || !value[0]) {
        lv_obj_set_hidden(value_,true);
        lv_obj_set_grid_cell(name_,LV_GRID_ALIGN_STRETCH,0,2,LV_GRID_ALIGN_CENTER,0,1);
    }
}
void MenuRow::toggle(bool checked) {
    lv_obj_set_hidden(value_, true);
    if (!toggle_) toggle_=lv_switch_create(root_);
    auto *control=toggle_; lv_obj_set_hidden(control,false);
    lv_obj_set_size(control, 32, 16);
    lv_obj_set_grid_cell(control, LV_GRID_ALIGN_END, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_set_style_bg_opa(control, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(control, lv_color_hex(0x475569), LV_PART_MAIN);
    lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(control, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(control, lv_color_hex(accent_), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(control, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_bg_color(control, lv_color_hex(0xe2e8f0), LV_PART_KNOB);
    lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_anim_duration(control, 0, 0);
    if (checked) lv_obj_add_state(control, LV_STATE_CHECKED);
    else lv_obj_remove_state(control, LV_STATE_CHECKED);
}
void MenuRow::slider(unsigned value, unsigned minimum, unsigned maximum) {
    lv_obj_set_hidden(value_, true);
    if (!slider_) slider_=lv_slider_create(root_);
    auto *control=slider_; lv_obj_set_hidden(control,false);
    lv_obj_set_size(control, SliderWidth, 5);
    lv_obj_set_grid_cell(control, LV_GRID_ALIGN_END, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_slider_set_range(control, minimum, maximum);
    lv_slider_set_value(control, value, LV_ANIM_OFF);
    for (auto part : {LV_PART_MAIN, LV_PART_INDICATOR, LV_PART_KNOB}) {
        lv_obj_set_style_bg_opa(control, LV_OPA_COVER, part);
        lv_obj_set_style_bg_color(control, lv_color_hex(part == LV_PART_MAIN ? 0x475569 : accent_), part);
        lv_obj_set_style_radius(control, LV_RADIUS_CIRCLE, part);
    }
    lv_obj_set_style_pad_all(control, 3, LV_PART_KNOB);
}
int MenuRow::contentWidth() const {
    return width_ - 2 * lv_obj_get_style_border_width(root_, LV_PART_MAIN) - lv_obj_get_style_pad_left(root_, LV_PART_MAIN) -
           lv_obj_get_style_pad_right(root_, LV_PART_MAIN);
}
int MenuRow::contentHeight() const {
    return height_ - 2 * lv_obj_get_style_border_width(root_, LV_PART_MAIN) - lv_obj_get_style_pad_top(root_, LV_PART_MAIN) -
           lv_obj_get_style_pad_bottom(root_, LV_PART_MAIN);
}
// Samples sit centred in the content box's height (where the texts are centred too), the name's at
// its left before the name, the value's Gap before the value text or the slider, whatever its length.
void MenuRow::nameSwatch(uint32_t rgb) {
    if (!root_) return;
    nameSwatch_.update(root_, 0, (contentHeight() - ColorSwatch::Size) / 2, rgb, ink_);
    lv_obj_set_style_pad_left(name_, ColorSwatch::Size + ColorSwatch::Gap, 0);
}
void MenuRow::valueSwatch(uint32_t rgb) {
    if (!root_) return;
    const bool slid = slider_ && !lv_obj_is_hidden(slider_);
    const int right = slid ? SliderWidth : lv_obj_is_hidden(value_) ? 0 : valueWidth_;
    valueSwatch_.update(root_, contentWidth() - right - ColorSwatch::Gap - ColorSwatch::Size, (contentHeight() - ColorSwatch::Size) / 2, rgb, ink_);
}
} // namespace openosc::ui
