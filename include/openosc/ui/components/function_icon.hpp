#pragma once
#include "lvgl.h"
#include "openosc/measurement/dmm/function.hpp"

namespace openosc::ui {
// The decoded function's sign on the DMM's function row (FunctionSymbols at the row's height, bare:
// the unit beside it names the quantity); unknown functions draw a question mark.
class FunctionIcon {
public:
    static constexpr int Width = 56, Height = 26;
    lv_obj_t *create(lv_obj_t *parent);
    void show(dmm::Function function, bool beeper, uint32_t color);
    void visible(bool value) { if (object_) lv_obj_set_hidden(object_, !value); }
    void place(int x, int y) { if (object_) lv_obj_set_pos(object_, x, y); }
    lv_obj_t *object() const { return object_; }
    dmm::Function function() const { return function_; }
    bool beeper() const { return beeper_; }

private:
    static void draw(lv_event_t *event);
    lv_obj_t *object_ = nullptr;
    dmm::Function function_ = dmm::Function::Unknown;
    bool beeper_ = false;
    uint32_t color_ = 0xffffff;
};
} // namespace openosc::ui
