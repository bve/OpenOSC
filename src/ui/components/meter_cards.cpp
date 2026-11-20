#include "openosc/ui/components/meter_cards.hpp"

namespace openosc::ui {
namespace {
using S = FunctionSymbols::Symbol;
} // namespace
void MeterCards::create(ParameterBar &bar) {
    bar_ = &bar;
    for (unsigned i = 0; i < ParameterBar::Count; ++i) {
        rows_[i] = lv_obj_create(bar.card(i));
        lv_obj_remove_style_all(rows_[i]);
        lv_obj_set_scrollable(rows_[i], false);
        lv_obj_set_pos(rows_[i], 0, 1); // The title line; the card's width follows its resizes.
        lv_obj_set_size(rows_[i], lv_pct(100), SignHeight + 1);
        lv_obj_add_event_cb(rows_[i], draw, LV_EVENT_DRAW_MAIN, this);
        lv_obj_add_event_cb(rows_[i], [](lv_event_t *event) {
            auto &self = *static_cast<MeterCards *>(lv_event_get_user_data(event));
            for (auto *&row : self.rows_) if (row == lv_event_get_target(event)) row = nullptr;
        }, LV_EVENT_DELETE, this);
        lv_obj_set_hidden(rows_[i], true);
    }
}
void MeterCards::visible(bool value) {
    for (auto *row : rows_) if (row) bar_->decorate(row, value);
}
unsigned MeterCards::signs(unsigned index, S (&output)[MaximumSigns]) const {
    switch (index) {
    case 0: output[0] = S::VoltsDc; output[1] = S::VoltsAc; return 2;
    case 1: output[0] = S::Ohm; output[1] = S::Continuity; output[2] = S::Diode; output[3] = S::Capacitor; return 4;
    case 2: output[0] = S::AmpsDc; output[1] = S::AmpsAc; return 2;
    case 3:
        if (currentInput_) { output[0] = S::MilliampsDc; output[1] = S::MilliampsAc; return 2; }
        output[0] = S::MillivoltsDc; output[1] = S::MillivoltsAc; output[2] = S::Temperature;
        return 3;
    default: return 0;
    }
}
void MeterCards::show(dmm::Function active, bool beeper, bool currentInput, uint32_t highlight, uint32_t muted) {
    const auto symbol = FunctionSymbols::of(active, beeper);
    if (symbol == active_ && currentInput == currentInput_ && highlight == highlight_ && muted == muted_) return;
    active_ = symbol; currentInput_ = currentInput; highlight_ = highlight; muted_ = muted;
    for (auto *row : rows_) if (row) lv_obj_invalidate(row);
}
void MeterCards::draw(lv_event_t *event) {
    auto &self = *static_cast<MeterCards *>(lv_event_get_user_data(event));
    auto *row = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    unsigned index = 0;
    while (index < ParameterBar::Count && self.rows_[index] != row) ++index;
    S signs[MaximumSigns];
    const unsigned count = self.signs(index, signs);
    lv_area_t area;
    lv_obj_get_coords(row, &area);
    // The F2 diode stands for kind 5 and 7 alike (the beeper shows on the function row's sign). On
    // the selected card's accent fill the decoded sign is the fill's ink and the others that ink
    // faded; on a grid card the palette's colours, the decoded one kept readable on the fill.
    const uint32_t fill = self.bar_->fill(index), ink = ParameterBar::ink(fill);
    const bool accented = self.bar_->accented(index);
    const uint32_t lit = accented ? ink : ParameterBar::readable(self.highlight_, fill);
    const uint32_t unlit = accented ? ParameterBar::faded(ink, fill) : self.muted_;
    const bool fineVoltage = index == 3 && count == 3;
    const int height = fineVoltage ? SignHeight - 2 : SignHeight;
    const int gap = fineVoltage ? 1 : Gap;
    int x = area.x1 + Inset;
    const int y = area.y1 + (SignHeight - height) / 2;
    for (unsigned i = 0; i < count; ++i) {
        const bool on = signs[i] == self.active_ || (signs[i] == S::Diode && self.active_ == S::DiodeBeeper);
        FunctionSymbols::draw(lv_event_get_layer(event), signs[i], x, y, height, lv_color_hex(on ? lit : unlit));
        x += FunctionSymbols::width(signs[i], height) + gap;
    }
}
} // namespace openosc::ui
