#pragma once
#include "openosc/ui/components/color_swatch.hpp"
#include "openosc/ui/components/elements.hpp"

namespace openosc::ui {
// Hardware keys edit the model; these native LVGL controls display its state.
class MenuRow {
public:
    static constexpr int SliderWidth = 86;
    // The row's default x, its border and horizontal padding: NameX is where its name starts in the parent.
    static constexpr int X = 6, Border = 1, Padding = 7, NameX = X + Border + Padding;
    MenuRow() = default;
    void update(lv_obj_t *parent, int y, int height, const char *name, const char *value,
                bool selected, const Theme &theme, uint32_t accent, int x = X, int width = 290);
    void visible(bool value) { if (root_) lv_obj_set_hidden(root_, !value); }
    MenuRow(lv_obj_t *parent, int y, int height, const char *name, const char *value,
            bool selected, const Theme &theme, uint32_t accent);
    void toggle(bool checked);
    void slider(unsigned value, unsigned minimum, unsigned maximum);
    // A colour sample before the name (an option of a colour list) or before the value (a colour
    // row; left of its slider when it has one), framed in the row's text colour. update() hides both.
    void nameSwatch(uint32_t rgb);
    void valueSwatch(uint32_t rgb);
    lv_obj_t *root() const { return root_; }                          // Tests: the row's box,
    lv_obj_t *swatch(bool value) const { return value ? valueSwatch_.object() : nameSwatch_.object(); } // its samples.

private:
    // The row's content box (its size less its border and padding): LVGL places a child, the
    // samples too, from its top left corner.
    int contentWidth() const;
    int contentHeight() const;
    lv_obj_t *root_ = nullptr, *name_ = nullptr, *value_ = nullptr;
    lv_obj_t *toggle_ = nullptr, *slider_ = nullptr;
    ColorSwatch nameSwatch_, valueSwatch_;
    uint32_t accent_, ink_ = 0;
    int height_ = 0, width_ = 0, valueWidth_ = 0;
};
} // namespace openosc::ui
