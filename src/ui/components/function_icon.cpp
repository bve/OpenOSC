#include "openosc/ui/components/function_icon.hpp"
#include "openosc/ui/components/function_symbols.hpp"

namespace openosc::ui {
lv_obj_t *FunctionIcon::create(lv_obj_t *parent) {
    object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(object_);
    lv_obj_set_size(object_, Width, Height);
    lv_obj_set_scrollable(object_, false);
    lv_obj_add_event_cb(object_, draw, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_event_cb(object_, [](lv_event_t *event) {
        static_cast<FunctionIcon *>(lv_event_get_user_data(event))->object_ = nullptr;
    }, LV_EVENT_DELETE, this);
    return object_;
}
void FunctionIcon::show(dmm::Function function, bool beeper, uint32_t color) {
    if (function == function_ && beeper == beeper_ && color == color_) return;
    function_ = function; beeper_ = beeper; color_ = color;
    if (object_) lv_obj_invalidate(object_);
}
void FunctionIcon::draw(lv_event_t *event) {
    auto &self = *static_cast<FunctionIcon *>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(self.object_, &area);
    FunctionSymbols::draw(lv_event_get_layer(event), FunctionSymbols::of(self.function_, self.beeper_), area.x1, area.y1,
                          Height, lv_color_hex(self.color_), true);
}
} // namespace openosc::ui
