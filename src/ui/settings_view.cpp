#include "openosc/ui/application_view.hpp"
#include <cstdio>

namespace openosc {
namespace {
constexpr uint32_t MicrosecondsPerSecond = 1000000;

void formatUptime(char (&text)[24], uint32_t seconds) {
    std::snprintf(text, sizeof text, "Up %02lu:%02lu:%02lu",
                  static_cast<unsigned long>(seconds / 3600),
                  static_cast<unsigned long>((seconds / 60) % 60),
                  static_cast<unsigned long>(seconds % 60));
}
} // namespace

void ApplicationView::uptime(uint64_t elapsedUs) {
    if (elapsedUs < (uint64_t(uptimeSeconds_) + 1) * MicrosecondsPerSecond) return;
    uptimeSeconds_ = static_cast<uint32_t>(elapsedUs / MicrosecondsPerSecond);
    if (menuUptime_) {
        char text[24];
        formatUptime(text, uptimeSeconds_);
        ui::Elements::setText(menuUptime_, text);
    }
}
// Every menu sits above the keys' cards (ScreenLayout::MenuKeysY, over the physical keys; their
// captions: ui::SoftKeys), the same frame for the instruments' panels, Settings and the image list;
// `instrument` only picks the accent. Its palette (Menu contrast, ui::MenuPalette) is resolved here
// once for everything the menu then draws; texts with no colour of their own take its text colour.
void ApplicationView::menuFrame(const char *title, bool instrument, bool retained, bool showUptime) {
    menuUptime_ = nullptr;
    host_.overlay(true);
    const uint32_t accent = instrument ? shown().elements[unsigned(mode_)][unsigned(ElementId::Parameters)].color : shown().accent;
    menuPalette_ = ui::MenuPalette::of(shown(), accent, menuContrast_);
    lv_obj_set_pos(menu_, 8, MenuTop);
    lv_obj_set_size(menu_, 304, MenuHeight);
    lv_obj_set_style_bg_color(menu_, lv_color_hex(menuPalette_.surface), 0);
    lv_obj_set_style_border_color(menu_, lv_color_hex(menuPalette_.frame), 0);
    lv_obj_set_style_text_color(menu_, lv_color_hex(menuPalette_.text), 0);
    lv_obj_set_hidden(menu_, false);
    lv_obj_set_style_border_width(menu_,1,0);
    if (!retained || !menuRetained_) {
        lv_obj_clean(menu_); menuContent_.reset(); choicePopup_.reset(); draftPreviewObject_=nullptr;
    }
    menuRetained_=retained;
    menuContent_.begin();
    if (draftPreviewObject_) lv_obj_set_hidden(draftPreviewObject_,true);
    auto *heading = retained ? menuContent_.text(menu_,title,10,4) : label(menu_, title, 10, 4);
    lv_obj_set_size(heading, showUptime ? 148 : 238, 22);
    lv_obj_set_style_text_font(heading, font(14), 0);
    lv_obj_set_style_text_color(heading, lv_color_hex(menuPalette_.accent), 0);
    if (showUptime) {
        char text[24];
        formatUptime(text, uptimeSeconds_);
        // Leave the Settings page counter (x = 252) its own column.
        menuUptime_ = retained ? menuContent_.text(menu_, text, 160, 7) : label(menu_, text, 160, 7);
        lv_obj_set_size(menuUptime_, 86, 20);
        lv_obj_set_style_text_font(menuUptime_, font(12), 0);
        lv_obj_set_style_text_color(menuUptime_, lv_color_hex(menuPalette_.secondary), 0);
        lv_obj_set_style_text_align(menuUptime_, LV_TEXT_ALIGN_RIGHT, 0);
    }
}
void ApplicationView::settings(const ui::SettingsMenu &menu, const ui::SettingsProperties &rows, const ui::PropertyNavigator &navigator,
                               const char *persistence) {
    menuFrame(menu.title(), false, true, true);
    const int top=ui::SettingsView(menu_, menuPalette_, &menuContent_).menu(menu, rows, navigator, persistence);
    choice(rows, navigator, top - 4);
}
void ApplicationView::information(const ui::SettingsMenu &menu, const DeviceInfo &info) {
    menuFrame(menu.title(), false, false, true);
    ui::SettingsView(menu_, menuPalette_).information(menu, info);
}
const ui::MenuPalette *ApplicationView::menuKeys() const {
    return menuContrast_ == MenuContrast::High && menu_ && !lv_obj_is_hidden(menu_) ? &menuPalette_ : nullptr;
}
} // namespace openosc
