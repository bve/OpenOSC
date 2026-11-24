#include "openosc/ui/components/meter_bar.hpp"
#include "openosc/ui/format/meter.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
        lv_obj_set_size(label, 1, 13); // Its width and place: scale().
    }
    state_ = Elements::label(parent, "NO DATA", StateX, 75);
    lv_obj_set_size(state_, StateWidth, 13);
    visible_ = true; status_ = DataStatus::NoData;
    decimals_ = 255; unit_[0] = 0;
    scale(dmm::BoardScale, 255, "");
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
// The end labels align with the ends of the bar. Move an intermediate label left only if a wide
// label to its right needs the space, such as the full-scale label with the engineering unit.
void MeterBar::scale(dmm::CountScale counts, unsigned decimals, const char *unit) {
    const bool known = decimals <= 6;
    scale_ = counts; decimals_ = uint8_t(known ? decimals : 255);
    std::snprintf(unit_, sizeof unit_, "%s", known ? unit : "");
    const int labels = counts.labels(), last = labels - 1;
    const int stateLeft = StateX, stateRight = StateX + StateWidth; // The caption's box, as before.
    int right = tickX(last, labels);
    for (int i = last; i >= 0; --i) {
        char text[24];
        MeterText::scaleNumber(counts.tickValue(i * counts.major), known ? decimals : 3, text, sizeof text);
        if (i == last && *unit_) {
            const size_t length = std::strlen(text);
            std::snprintf(text + length, sizeof text - length, " %s", unit_);
        }
        lv_point_t size;
        lv_text_get_size(&size, text, Elements::font(12), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        const int width = std::max(1, int(size.x));
        const int tick = tickX(i, labels);
        const int ideal = i == last ? tick - width : i ? tick - width / 2 : tick;
        const int left = std::min(ideal, right - width);
        auto *label = ticks_[i];
        Elements::setText(label, text);
        lv_obj_set_width(label, width);
        lv_obj_set_x(label, left);
        covered_[i] = left < stateRight && left + width > stateLeft;
        right = left - LabelGap;
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
    auto value = dmm::BarReading::from(result,health,relative,axis);
    value.limitToRange(result);
    const bool over = value.status == DataStatus::Overload;
    const auto color = over ? overload_ : value.status == DataStatus::Ok ? accent : 0x64748b;
    const int magnitude = std::abs(value.counts);
    const int full = value.scale.fullScale;
    const int lit = over ? Segments : std::min(Segments, (magnitude * Segments + full - 1) / full);
    // The numbers follow the range: a scale, decimals or unit of their own (the same counts at 2.5 V
    // and 25 V differ by their decimals); unknown decimals keep the range's numbers on the same scale.
    const unsigned decimals = value.decimals(result);
    const char *unit = MeterText::unit(value.unit(result));
    if (decimals <= 6 ? value.scale != scale_ || decimals != decimals_ || std::strcmp(unit, unit_) : value.scale != scale_)
        scale(value.scale, decimals, unit);
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
