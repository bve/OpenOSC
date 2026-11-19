#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/settings/theme.hpp"

namespace openosc::ui {
// "Hold to power off" with a progress bar, on LVGL's top layer: above every screen, menu, panel and
// dialog. Only drawing; the hold itself is PowerHold and the controller's.
// Created on its first show (most sessions never hold the key), deleted with the view or a rebuild.
class PowerHoldOverlay {
public:
    ~PowerHoldOverlay() { clear(); }
    void clear() { if (root_) lv_obj_delete(root_); root_ = bar_ = title_ = nullptr; shown_ = -1; }
    void theme(const Theme &theme) { clear(); theme_ = &theme; }
    void show(int percent, const char *title); // -1 hides.
private:
    void create();
    const Theme *theme_ = nullptr;
    lv_obj_t *root_ = nullptr, *bar_ = nullptr, *title_ = nullptr;
    int shown_ = -1;
};
} // namespace openosc::ui
