#include "openosc/ui/settings/view.hpp"
#include "openosc/ui/components/property_rows.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::ui {
lv_obj_t *SettingsView::text(const char *value, int x, int y, int width, unsigned size) {
    auto *label = content_ ? content_->text(parent_,value,x,y) : Elements::label(parent_, value, x, y);
    lv_obj_set_size(label, width, size + 5);
    lv_obj_set_style_text_font(label, Elements::font(size), 0);
    return label;
}
void SettingsView::breadcrumb(const SettingsMenu &menu) {
    auto *path = text(menu.path(), 10, 29, 282);
    lv_obj_set_style_text_color(path, lv_color_hex(palette_.secondary), 0);
}
int SettingsView::menu(const SettingsMenu &menu, const SettingsProperties &rows, const PropertyNavigator &navigator,
                       const char *persistence) {
    breadcrumb(menu);
    int firstY = 54;
    const char *header = menu.page() == SettingsPage::Files ? persistence
        : menu.page() == SettingsPage::Appearance ? (rows.preferences().useBuiltinTheme ? "Theme: Built-in" : "Theme: Custom JSON")
        : menu.page() == SettingsPage::Performance ? "USB data only at 200 MHz, no restart"
        : menu.page() == SettingsPage::Power ? "USB / active output inhibit auto off"
        : menu.page() == SettingsPage::Calibration ? "Zero: ground inputs. Gain: DC reference"
        : menu.page() == SettingsPage::Gain ? "Volts at the probe tip, ratio once"
        : menu.page() == SettingsPage::Interleave ? "HIGH ADC offset, new CH1 records"
        : menu.page() == SettingsPage::ScopeKeys ? "AUTO and F2/F4 holds on the Scope screen"
        : menu.page() == SettingsPage::PowerOnKeys ? "Hold with REL while switching on" : nullptr;
    if (header) { text(header, 10, 46, 282); firstY = 68; }
    // Four rows within the menu above the cards, also under a header line (68 + 4 x 24 = 164).
    const PropertyRows page{firstY, 24, 22, 4};
    if (menu.count() > page.visible) {
        char counter[24];
        std::snprintf(counter, sizeof counter, "%u/%u", navigator.row() / page.visible + 1, (menu.count() + page.visible - 1) / page.visible);
        auto *label = text(counter, 252, 7, 40);
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    }
    MenuContent local;
    page.draw(content_ ? *content_ : local, parent_, rows, navigator, palette_);
    return firstY;
}
} // namespace openosc::ui
