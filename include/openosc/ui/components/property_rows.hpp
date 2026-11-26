#pragma once
#include "openosc/ui/components/menu_content.hpp"
#include "openosc/ui/editors/property_navigator.hpp"

namespace openosc::ui {
// The visible page of a property list as menu rows, each drawn by its kind: a switch for a Toggle,
// a slider for a Number with a range, ">" for a Page, the value of a Number being edited between
// arrows. The page holds the navigator's row.
struct PropertyRows {
    int top, pitch, height;
    unsigned visible;
    void draw(MenuContent &content, lv_obj_t *parent, const PropertyList &list, const PropertyNavigator &navigator,
              const MenuPalette &palette) const;
    unsigned first(unsigned row) const { return row / visible * visible; }
};
} // namespace openosc::ui
