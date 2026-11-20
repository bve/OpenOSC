#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/power/battery.hpp"

namespace openosc::ui {
class BatteryIndicator {
public:
    void clear() { if (root_) lv_obj_delete(root_); }
    void create(lv_obj_t *parent);
    void update(const power::BatteryState &state);
    void display(uint32_t mode); // 0 icon, 1 volts, 2 percent.
    bool text() const { return mode_ != 0; } // Volts or percent: the wider column (StatusBarLayout::BatteryText).
private:
    void applyMode();
    lv_obj_t *root_=nullptr,*bar_=nullptr,*cap_=nullptr,*symbol_=nullptr,*text_=nullptr;
    power::BatteryState state_;
    uint32_t cached_=0xffffffffU,mode_=0;
};
} // namespace openosc::ui
