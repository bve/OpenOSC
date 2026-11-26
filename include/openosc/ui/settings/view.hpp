#pragma once
#include "openosc/settings/preferences.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/settings/menu.hpp"
#include "openosc/ui/components/menu_content.hpp"
#include "openosc/ui/settings/properties.hpp"
#include "openosc/ui/editors/property_navigator.hpp"

namespace openosc {
struct DeviceInfo {
    uint32_t jedec = 0, capacity = 0, freeBytes = 0;
    bool storageReady = false;
};
namespace ui {
class SettingsView {
public:
    SettingsView(lv_obj_t *parent, const MenuPalette &palette, MenuContent *content = nullptr) : content_(content), parent_(parent), palette_(palette) {}
    // The page's rows, the navigator's row on show; returns where its rows begin (for a choice list).
    int menu(const SettingsMenu &menu, const SettingsProperties &rows, const PropertyNavigator &navigator, const char *persistence);
    void information(const SettingsMenu &menu, const DeviceInfo &info);

private:
    lv_obj_t *text(const char *value, int x, int y, int width = 280, unsigned size = 12);
    void breadcrumb(const SettingsMenu &menu);
    MenuContent *content_;
    lv_obj_t *parent_;
    const MenuPalette &palette_;
};
} // namespace ui
} // namespace openosc
