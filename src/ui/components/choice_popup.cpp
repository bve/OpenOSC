#include "openosc/ui/components/choice_popup.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::ui {
void ChoicePopup::show(lv_obj_t *menu, int top, const PropertyList &list, unsigned row, unsigned draft, const MenuPalette &palette) {
    constexpr int RowHeight = 24, Width = 200;
    if (row >= list.count() || list.kind(row) != PropertyKind::Choice || !list.options(row)) { hide(); return; } // Gone, changed or emptied: the navigator closes it at the next key.
    if (!box_) {
        box_ = lv_obj_create(menu);
        lv_obj_remove_style_all(box_);
        lv_obj_set_scrollable(box_, false);
        counter_ = Elements::label(box_, "", 0, 2);
    }
    const unsigned options = list.options(row), current = list.option(row);
    if (draft >= options) draft = 0; // The list shrank while open (the navigator clamps at the next key).
    const unsigned first = draft / Visible * Visible, shown = options - first < Visible ? options - first : Visible;
    // Within the menu: a list near its bottom opens upwards as far as it needs.
    const int height = 22 + int(shown) * RowHeight, room = lv_obj_get_height(menu) - 4;
    lv_obj_set_pos(box_, 304 - Width - 8, top + height > room ? (room - height > 2 ? room - height : 2) : top);
    lv_obj_set_size(box_, Width, height);
    lv_obj_set_style_bg_color(box_, lv_color_hex(palette.surface), 0);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(box_, 1, 0);
    lv_obj_set_style_border_color(box_, lv_color_hex(palette.accent), 0);
    lv_obj_set_style_radius(box_, 3, 0);
    lv_obj_set_hidden(box_, false);
    lv_obj_move_foreground(box_);
    char page[16];
    std::snprintf(page, sizeof page, "%u/%u", draft + 1, options);
    if (std::strcmp(lv_label_get_text(counter_), page)) lv_label_set_text(counter_, page);
    lv_obj_set_pos(counter_, 8, 3);
    lv_obj_set_style_text_color(counter_, lv_color_hex(palette.secondary), 0);
    for (unsigned i = 0; i < Visible; ++i) {
        if (i >= shown) { rows_[i].visible(false); continue; }
        char name[48], shown[52];
        list.optionName(row, first + i, name, sizeof name);
        std::snprintf(shown, sizeof shown, first + i == current ? "%s <" : "%s", name); // The option in force.
        rows_[i].update(box_, 20 + int(i) * RowHeight, RowHeight - 2, shown, "", first + i == draft, palette, 2, Width - 6);
        if (uint32_t rgb; list.optionSwatch(row, first + i, rgb)) rows_[i].nameSwatch(rgb); // Colour lists only.
    }
}
} // namespace openosc::ui
