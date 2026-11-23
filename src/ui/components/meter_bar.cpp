#include "openosc/ui/components/meter_bar.hpp"
#include "openosc/ui/format/meter.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace openosc::ui {
void MeterBar::draw(lv_event_t *event) {
    auto &self = *static_cast<MeterBar *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(self.bar_, &area);
    lv_draw_rect_dsc_t on, off;
    lv_draw_rect_dsc_init(&on); lv_draw_rect_dsc_init(&off);
    on.bg_color = lv_color_hex(self.color_); off.bg_color = lv_color_hex(self.off_);
    for (int i = 0; i < Segments; ++i) {
        lv_area_t segment = {lv_coord_t(area.x1 + i * Pitch), area.y1, lv_coord_t(area.x1 + i * Pitch + SegmentWidth - 1), area.y2};
        lv_draw_rect(layer, i < self.lit_ ? &on : &off, &segment);
    }
}
void MeterBar::create(lv_obj_t *parent) {
    initialized_ = false; lit_ = 0;
    sign_ = Elements::label(parent, "", 0, 63);
    lv_obj_set_size(sign_, BarX, 14);
    bar_ = lv_obj_create(parent);
    lv_obj_remove_style_all(bar_);
    lv_obj_set_pos(bar_, BarX, 65);
    lv_obj_set_size(bar_, Segments * Pitch - 1, Height);
    lv_obj_set_scrollable(bar_, false);
    lv_obj_add_event_cb(bar_, draw, LV_EVENT_DRAW_MAIN, this);
    for (auto *&label : ticks_) {
        label = Elements::label(parent, "", 0, 75);
        lv_obj_set_size(label, TickWidth, 13);
    }
    state_ = Elements::label(parent, "NO DATA", StateX, 75);
    lv_obj_set_size(state_, StateWidth, 13);
    visible_ = true; status_ = DataStatus::NoData;
    scale(dmm::BoardScale);
    lv_obj_set_style_text_align(state_, LV_TEXT_ALIGN_CENTER, 0);
    palette({0, 0, muted_, 0, off_, 0, overload_});
}
void MeterBar::palette(const MeterPalette &palette) {
    const bool repaint = palette.line != off_ || palette.alert != overload_;
    off_ = palette.line; overload_ = palette.alert; muted_ = palette.muted;
    Elements::textColor(sign_, muted_);
    for (auto *tick : ticks_) Elements::textColor(tick, muted_);
    Elements::textColor(state_, MeterText::alert(status_) ? overload_ : muted_);
    if (repaint) { initialized_ = false; lv_obj_invalidate(bar_); } // The OL colour is in color_.
}
void MeterBar::top(int y) {
    lv_obj_set_y(sign_, y);
    lv_obj_set_y(bar_, y + 2);
    lv_obj_set_y(state_, y + 12);
    for (auto *tick : ticks_) lv_obj_set_y(tick, y + 12);
}
void MeterBar::visible(bool value) {
    for (auto *object : {bar_, state_, sign_}) lv_obj_set_hidden(object, !value);
    visible_ = value;
    showTicks();
}
// A label every major tick along the bar in thousands of counts (one decimal where a scale's numbers
// are not whole: the diode's 1.1 V steps), the first left-aligned at zero, the last right-aligned at
// full scale; labels a smaller scale does not use stay hidden.
void MeterBar::scale(dmm::CountScale counts, bool temperature) {
    scale_ = counts;
    const int last = counts.labels() - 1;
    const bool tenths = !temperature && (counts.major * counts.tickStep) % 1000;
    const int labelWidth = temperature || tenths ? 38 : TickWidth;
    const int divisor = temperature ? dmm::TemperatureAxis::CountsPerDegree : 1000;
    for (int i = 0; i <= last; ++i) {
        const int value = counts.tickValue(i * counts.major);
        char text[12];
        if (tenths && value % 1000) std::snprintf(text, sizeof text, "%s%d.%d", value < 0 ? "-" : "", std::abs(value) / 1000, std::abs(value) % 1000 / 100);
        else std::snprintf(text, sizeof text, "%d", value / divisor);
        const int tick = BarX + i * Segments / last * Pitch;
        const int left = i == last ? tick - labelWidth : i ? tick - labelWidth / 2 : tick;
        auto *label = ticks_[i];
        lv_label_set_text(label, text);
        lv_obj_set_width(label, labelWidth);
        lv_obj_set_x(label, left);
        covered_[i] = left < StateX + StateWidth && left + labelWidth > StateX;
        lv_obj_set_style_text_align(label, i == last ? LV_TEXT_ALIGN_RIGHT : i ? LV_TEXT_ALIGN_CENTER : LV_TEXT_ALIGN_LEFT, 0);
    }
    showTicks();
}
void MeterBar::showTicks() {
    const bool caption = status_ != DataStatus::Ok;
    for (int i = 0; i < dmm::MaxScaleLabels; ++i)
        lv_obj_set_hidden(ticks_[i], !visible_ || i >= scale_.labels() || (caption && covered_[i]));
}
void MeterBar::update(const dmm::Result &result, DataStatus health, uint32_t accent,
                      const dmm::RelativeReading *relative, dmm::TemperatureAxis *axis) {
    const auto value = dmm::BarReading::from(result,health,relative,axis);
    const bool over = value.status == DataStatus::Overload;
    const auto color = over ? overload_ : value.status == DataStatus::Ok ? accent : 0x64748b;
    const int magnitude = std::abs(value.counts);
    const int full = value.scale.fullScale;
    const int lit = over ? Segments : std::min(Segments, (magnitude * Segments + full - 1) / full);
    if (value.scale != scale_) scale(value.scale, value.temperature);
    const bool negative = value.counts < 0;
    if (initialized_ && lit == lit_ && negative == negative_ && status_ == value.status && color_ == color) return;
    const bool repaint = !initialized_ || lit != lit_ || color_ != color;
    if (!initialized_ || negative != negative_) lv_label_set_text(sign_, negative ? "-" : "");
    const bool statusChanged = !initialized_ || status_ != value.status;
    if (statusChanged) {
        lv_label_set_text(state_, MeterText::caption(value.status));
        Elements::textColor(state_, MeterText::alert(value.status) ? overload_ : muted_);
    }
    initialized_ = true; lit_ = lit; negative_ = negative; status_ = value.status; color_ = color;
    if (statusChanged) showTicks();
    if (repaint) lv_obj_invalidate(bar_);
}
}
