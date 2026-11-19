#include "openosc/ui/components/property_rows.hpp"
#include <cstdio>

namespace openosc::ui {
void PropertyRows::draw(MenuContent &content, lv_obj_t *parent, const PropertyList &list, const PropertyNavigator &navigator,
                        const Theme &theme, uint32_t accent) const {
    const unsigned start = first(navigator.row());
    for (unsigned i = start; i < list.count() && i < start + visible; ++i) {
        char name[64], current[60], value[68];
        list.name(i, name, sizeof name);
        list.value(i, current, sizeof current);
        const auto kind = list.kind(i);
        const bool editing = i == navigator.row() && navigator.mode() == PropertyNavigator::Mode::Number;
        std::snprintf(value, sizeof value, editing ? "< %s >" : "%s", kind == PropertyKind::Page && !current[0] ? ">" : current);
        auto &row = content.row(parent, top + int(i - start) * pitch, height, name, value, i == navigator.row(), theme, accent);
        int32_t minimum = 0, maximum = 0;
        if (kind == PropertyKind::Toggle) row.toggle(list.option(i));
        else if (kind == PropertyKind::Number && !editing && list.range(i, minimum, maximum))
            row.slider(unsigned(list.number(i) - minimum), 0, unsigned(maximum - minimum));
        if (uint32_t rgb; list.valueSwatch(i, rgb)) row.valueSwatch(rgb); // Colour rows only.
    }
    // Another page follows: said under the last row, where the menu has room for it.
    const int below = top + int(visible) * pitch;
    if (start + visible < list.count() && below + 14 <= lv_obj_get_height(parent)) {
        auto *more = content.text(parent, "More \u2193", MenuRow::NameX, below); // In line with the row names.
        lv_obj_set_size(more, 100, 14);
        lv_obj_set_style_text_color(more, lv_color_hex(accent), 0);
    }
}
} // namespace openosc::ui
