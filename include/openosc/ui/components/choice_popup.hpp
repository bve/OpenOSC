#pragma once
#include "openosc/ui/components/menu_row.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// The open list of a Choice row: its options a page at a time around the draft, the draft
// highlighted and the option in force marked "<". Created once on its menu and only hidden when closed,
// so opening it again adds no objects.
class ChoicePopup {
public:
    static constexpr unsigned Visible = 5;
    void reset() { box_ = nullptr; for (auto &row : rows_) row = MenuRow{}; } // Its menu was cleaned.
    void hide() { if (box_) lv_obj_set_hidden(box_, true); }
    void show(lv_obj_t *menu, int top, const PropertyList &list, unsigned row, unsigned draft, const MenuPalette &palette);
private:
    lv_obj_t *box_ = nullptr, *counter_ = nullptr;
    MenuRow rows_[Visible];
};
} // namespace openosc::ui
