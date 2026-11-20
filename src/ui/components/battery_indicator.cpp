#include "openosc/ui/components/battery_indicator.hpp"
#include "openosc/ui/components/status_bar_layout.hpp"
#include <cstdio>

namespace openosc::ui {
void BatteryIndicator::create(lv_obj_t *parent) {
    clear();
    root_=lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_,StatusBarLayout::BatteryIcon.width,14);
    lv_obj_set_scrollable(root_,false);
    lv_obj_add_event_cb(root_,[](lv_event_t *event) {
        static_cast<BatteryIndicator*>(lv_event_get_user_data(event))->root_=nullptr;
    },LV_EVENT_DELETE,this);
    bar_=lv_bar_create(root_);
    lv_obj_remove_style_all(bar_);
    lv_obj_set_pos(bar_,0,2); lv_obj_set_size(bar_,13,10);
    lv_bar_set_range(bar_,0,5);
    lv_obj_set_style_border_width(bar_,1,0);
    lv_obj_set_style_border_color(bar_,lv_color_hex(0x94a3b8),0);
    lv_obj_set_style_pad_all(bar_,2,0);
    lv_obj_set_style_bg_opa(bar_,LV_OPA_COVER,LV_PART_INDICATOR);
    cap_=lv_obj_create(root_);
    lv_obj_remove_style_all(cap_);
    lv_obj_set_pos(cap_,13,5); lv_obj_set_size(cap_,2,4);
    lv_obj_set_style_bg_color(cap_,lv_color_hex(0x94a3b8),0);
    lv_obj_set_style_bg_opa(cap_,LV_OPA_COVER,0);
    symbol_=Elements::label(root_,"?",0,0);
    lv_obj_set_size(symbol_,13,14);
    lv_obj_set_style_text_font(symbol_,Elements::font(12),0);
    lv_obj_set_style_text_align(symbol_,LV_TEXT_ALIGN_CENTER,0);
    text_=Elements::label(root_,"",0,1);
    lv_obj_set_size(text_,StatusBarLayout::BatteryText.width,14);
    lv_obj_set_style_text_font(text_,Elements::font(12),0);
    lv_obj_set_style_text_align(text_,LV_TEXT_ALIGN_RIGHT,0); // Ends at the panel edge, like the icon.
    applyMode(); update(state_);
}
void BatteryIndicator::applyMode() {
    if (!root_) return;
    const bool icon=mode_==0;
    const auto column=StatusBarLayout::battery(!icon);
    lv_obj_set_pos(root_,column.x,3); lv_obj_set_size(root_,column.width,14);
    lv_obj_set_hidden(bar_,!icon); lv_obj_set_hidden(cap_,!icon);
    lv_obj_set_hidden(symbol_,!icon); lv_obj_set_hidden(text_,icon);
    cached_=0xffffffffU;
}
void BatteryIndicator::display(uint32_t mode) {
    if (mode==mode_) return;
    mode_=mode; applyMode(); update(state_);
}
void BatteryIndicator::update(const power::BatteryState &state) {
    state_=state;
    const uint32_t code=uint32_t(state.bars)|(uint32_t(state.valid)<<3)|(uint32_t(state.low)<<4)|
        (uint32_t(state.external)<<5)|(uint32_t(state.full)<<6)|(uint32_t(state.measured)<<7)|
        (mode_<<8)|(uint32_t(mode_ ? state.millivolts/10 : 0)<<12)|(uint32_t(mode_ ? state.percent : 0)<<24);
    if (code==cached_ || !root_) return;
    cached_=code;
    const bool charging=state.external && !state.full;
    if (mode_) {
        char text[12];
        if (charging && (mode_!=2 || !state.measured || state.percent<100)) std::snprintf(text,sizeof text,"CHG");
        else if (!state.measured) std::snprintf(text,sizeof text,mode_==1 ? "--V" : "--%%");
        else if (mode_==1) std::snprintf(text,sizeof text,"%u.%02uV",state.millivolts/1000U,(state.millivolts%1000U)/10U);
        else std::snprintf(text,sizeof text,"%u%%",state.percent);
        lv_label_set_text(text_,text);
        lv_obj_set_style_text_color(text_,lv_color_hex(state.low ? 0xf87171 : 0x94a3b8),0);
        return;
    }
    lv_bar_set_value(bar_,state.valid && !charging ? state.bars : 0,LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_,lv_color_hex(state.low ? 0xf87171 : 0x4ade80),LV_PART_INDICATOR);
    // One bar is narrower than a pixel at this size: a red outline is what tells "low" from empty.
    const auto outline=lv_color_hex(state.low ? 0xf87171 : 0x94a3b8);
    lv_obj_set_style_border_color(bar_,outline,0);
    lv_obj_set_style_bg_color(cap_,outline,0);
    lv_label_set_text(symbol_,charging ? "+" : !state.valid ? "?" : "");
}
} // namespace openosc::ui
