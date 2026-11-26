#include "openosc/ui/application_view.hpp"
#include "openosc/ui/components/property_rows.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/components/menu_row.hpp"
#include <cstdio>
#include <cstring>

namespace openosc {
void ApplicationView::properties(const ui::PropertyList &properties, unsigned selected) {
    ui::PropertyNavigator browse;
    browse.reset(selected);
    this->properties(properties, browse);
}
void ApplicationView::properties(const ui::PropertyList &properties, const ui::PropertyNavigator &navigator) {
    menuFrame(properties.title(), true, true);
    if (mode_ == MeasurementMode::Oscilloscope)
        host_.parameters().select(!std::strcmp(properties.title(), "TRIGGER") ? 2 : !std::strcmp(properties.title(), "CHANNEL") ? 0 : 3);
    else if (mode_ == MeasurementMode::Generator)
        host_.parameters().select(!std::strcmp(properties.title(), "WAVEFORM") ? 0 : 2);
    const unsigned visible=properties.preview() ? 3 : properties.notice() ? 4 : 5;
    const ui::PropertyRows rows{properties.notice() ? 50 : 30, 26, 25, visible};
    if (properties.notice()) {
        auto *notice=menuContent_.text(menu_,properties.notice(),10,29);
        lv_obj_set_size(notice,282,16);
        lv_obj_set_style_text_color(notice,lv_color_hex(menuPalette_.secondary),0);
    }
    char page[24];
    std::snprintf(page,sizeof page,"%u/%u",navigator.row()/visible+1,(properties.count()+visible-1)/visible);
    auto *counter = menuContent_.text(menu_, page, 252, 7);
    lv_obj_set_size(counter, 40, 16);
    lv_obj_set_style_text_align(counter, LV_TEXT_ALIGN_RIGHT, 0);
    rows.draw(menuContent_, menu_, properties, navigator, menuPalette_);
    if (const auto *draft=properties.preview()) {
        if (!draftPreviewObject_) draftPreviewObject_=draftPreview_.create(menu_);
        lv_obj_set_pos(draftPreviewObject_,10,130); lv_obj_set_size(draftPreviewObject_,284,36);
        lv_obj_set_hidden(draftPreviewObject_,false);
        draftPreview_.update(*draft,shown().generator);
    }
    choice(properties, navigator, 24);
}
void ApplicationView::choice(const ui::PropertyList &list, const ui::PropertyNavigator &navigator, int top) {
    if (navigator.mode() == ui::PropertyNavigator::Mode::Choice)
        choicePopup_.show(menu_, top, list, navigator.row(), navigator.draft(), menuPalette_);
    else choicePopup_.hide();
}
void ApplicationView::frequencyEditor(const ui::FrequencyEditor &editor) {
    menuFrame("FREQUENCY", true);
    host_.parameters().select(1);
    char number[24], step[32], text[64];
    editor.text(number, sizeof number);
    auto *value = label(menu_, number, 10, 34);
    lv_obj_set_size(value, 254, 44);
    lv_obj_set_style_text_font(value, font(32), 0);
    lv_obj_update_layout(value);
    lv_point_t position;
    lv_label_get_letter_pos(value, editor.digit(), &position);
    auto *marker = lv_obj_create(menu_);
    lv_obj_remove_style_all(marker);
    lv_obj_set_pos(marker, 9 + position.x, 33);
    lv_obj_set_size(marker, lv_font_get_glyph_width(font(32), number[editor.digit()], 0) + 2, 40);
    lv_obj_set_style_border_width(marker, 1, 0);
    lv_obj_set_style_border_color(marker, lv_color_hex(menuPalette_.accent), 0);
    auto *unit = label(menu_, editor.unit(), 234, 43);
    lv_obj_set_style_text_font(unit, font(24), 0);
    lv_obj_set_size(unit, 60, 32);
    lv_obj_set_style_text_align(unit, LV_TEXT_ALIGN_RIGHT, 0);
    plot::Format::quantity(editor.step() / 1000.0, "Hz", step, sizeof step);
    ui::MenuRow(menu_, 87, 25, "Unit", editor.unit(), false, menuPalette_);
    ui::MenuRow(menu_, 113, 25, "Step", step, true, menuPalette_);
    plot::Format::quantity(editor.value() / 1000.0, "Hz", text, sizeof text);
    ui::MenuRow(menu_, 139, 25, "Draft", text, false, menuPalette_);
}
} // namespace openosc
