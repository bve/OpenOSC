#include "openosc/ui/waveform.hpp"
#include "openosc/ui/components/elements.hpp"
#include "src/core/lv_obj_event_private.h" // lv_cover_check_info_t: LVGL's cover-check protocol.
#include <cstring>

namespace openosc {
lv_obj_t *WaveformWidget::create(lv_obj_t *parent) {
    held_ = false;
    fadeAt_ = frozenAt_ = 0;
    model_.reset();
    object_ = lv_obj_create(parent);
    lv_obj_remove_style_all(object_);
    lv_obj_set_scrollable(object_, false);
    lv_obj_set_size(object_, 320, 152);
    lv_obj_add_event_cb(object_, draw, LV_EVENT_DRAW_MAIN, this);
    lv_obj_add_event_cb(object_, cover, LV_EVENT_COVER_CHECK, this);
    lv_obj_add_event_cb(object_, geometry, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_add_event_cb(object_, geometry, LV_EVENT_DELETE, this);
    note_ = lv_label_create(object_); noteLow_ = false;
    lv_obj_remove_style_all(note_);
    lv_obj_set_style_text_font(note_, ui::Elements::font(12), 0);
    lv_obj_set_style_bg_opa(note_, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(note_, 2, 0);
    // Inside the plot's top right corner, clear of the XY/REF marks there.
    lv_obj_align(note_, LV_ALIGN_TOP_RIGHT, -(plot::PlotFrame::Right + 30), plot::PlotFrame::Top + 1);
    lv_obj_set_hidden(note_, true);
    counter_ = lv_label_create(object_);
    lv_obj_remove_style_all(counter_);
    lv_obj_set_style_text_font(counter_, ui::Elements::font(14), 0);
    lv_obj_set_style_bg_opa(counter_, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(counter_, 2, 0);
    lv_obj_align(counter_, LV_ALIGN_TOP_LEFT, plot::PlotFrame::Left + 1, plot::PlotFrame::Top + 1);
    lv_obj_set_hidden(counter_, true);
    return object_;
}
void WaveformWidget::colors(const Theme &theme) {
    palette_.background = theme.background;
    palette_.grid = theme.grid;
    palette_.channels[0] = theme.channel1;
    palette_.channels[1] = theme.channel2;
    palette_.math = theme.math;
    palette_.reference = theme.reference;
    palette_.cursor = theme.cursor;
    palette_.readout = theme.foreground; // Without an appearance: neutral texts in the theme's text colour.
    if (object_)
        lv_obj_invalidate(object_);
}
void WaveformWidget::roles(uint32_t trigger, uint32_t tagText, uint32_t readout) {
    if (palette_.trigger == trigger && palette_.tagText == tagText && palette_.readout == readout) return;
    palette_.trigger = trigger; palette_.tagText = tagText; palette_.readout = readout;
    if (object_) lv_obj_invalidate(object_);
}
bool WaveformWidget::configure(const plot::Options &options) {
    if (!model_.configure(options))
        return false;
    if (object_)
        lv_obj_invalidate(object_);
    return true;
}
void WaveformWidget::clear() {
    model_.clear();
    if (object_)
        lv_obj_invalidate(object_);
}
void WaveformWidget::held(bool value) {
    model_.hold(value, lv_tick_get());
    if (value && !held_) {
        frozenAt_ = lv_tick_get();
        if (model_.agePersistence(frozenAt_) && object_) lv_obj_invalidate(object_);
    }
    held_ = value;
}
void WaveformWidget::trigger(scope::Channel channel, double codes) {
    model_.trigger(channel, codes);
    if (object_)
        lv_obj_invalidate(object_);
}
bool WaveformWidget::cursorLines(const plot::CursorOptions &cursors) {
    if (!model_.cursorLines(cursors)) return false;
    if (object_) lv_obj_invalidate(object_);
    return true;
}
void WaveformWidget::cursorFocus(plot::CursorLine line) {
    if (model_.cursorFocus() == line) return;
    model_.cursorFocus(line);
    if (object_) lv_obj_invalidate(object_);
}
void WaveformWidget::frontChannel(unsigned channel) {
    if (model_.frontChannel() == (channel & 1)) return;
    model_.frontChannel(channel);
    if (object_) lv_obj_invalidate(object_);
}
// A label over the plot on the plot's background; empty hides it.
static void overlayLabel(lv_obj_t *label, const char *text, uint32_t color, uint32_t background) {
    if (!label) return;
    if (std::strcmp(lv_label_get_text(label), text)) lv_label_set_text(label, text);
    ui::Elements::textColor(label, color);
    if (!lv_color_eq(lv_obj_get_style_bg_color(label, LV_PART_MAIN), lv_color_hex(background)))
        lv_obj_set_style_bg_color(label, lv_color_hex(background), 0);
    lv_obj_set_hidden(label, !*text);
}
void WaveformWidget::note(const char *text, uint32_t color) {
    const bool low = options().mode == plot::Mode::SpectrumOverlay;
    if (note_ && low != noteLow_) {
        noteLow_ = low;
        if (low) lv_obj_align(note_, LV_ALIGN_BOTTOM_LEFT, plot::PlotFrame::Left + 1, -(plot::PlotFrame::Bottom + 1));
        else lv_obj_align(note_, LV_ALIGN_TOP_RIGHT, -(plot::PlotFrame::Right + 30), plot::PlotFrame::Top + 1);
    }
    overlayLabel(note_, text, color, palette_.background);
}
void WaveformWidget::counter(const char *text, uint32_t color) { overlayLabel(counter_, text, color, palette_.background); }
void WaveformWidget::setFrame(const scope::FrameView &frame) {
    if (!object_)
        return;
    lv_obj_update_layout(object_);
    const auto before = model_.statistics().projections;
    const bool dirty = model_.needsFrame();
    model_.project(frame, plot::PlotFrame::plotWidth(lv_obj_get_width(object_)), plot::PlotFrame::plotHeight(lv_obj_get_height(object_)),
                   held_ ? frozenAt_ : lv_tick_get(), held_ ? nullptr : cache_);
    if (dirty || model_.needsFrame() || before != model_.statistics().projections)
        lv_obj_invalidate(object_);
}
void WaveformWidget::service(uint32_t now) {
    // The fade pass: only a finite persistence fades (an infinite one changes only with a frame).
    if (!object_ || held_ || !model_.options().persistenceMs ||
        model_.options().persistenceMs == plot::InfinitePersistence || !model_.hasFrame())
        return;
    if (uint32_t(now - fadeAt_) < 50)
        return;
    fadeAt_ = now;
    if (model_.agePersistence(now) && lv_obj_is_visible(object_)) lv_obj_invalidate(object_);
}
bool WaveformWidget::captureReference(unsigned channel) {
    if (!model_.captureReference(channel))
        return false;
    if (object_)
        lv_obj_invalidate(object_);
    return true;
}
void WaveformWidget::clearReference() {
    model_.clearReference();
    if (object_)
        lv_obj_invalidate(object_);
}
void WaveformWidget::clearHistory() {
    model_.clearHistory();
    if (object_)
        lv_obj_invalidate(object_);
}
lv_area_t WaveformWidget::plotArea() const {
    lv_area_t area{};
    if (object_) lv_obj_get_coords(object_, &area);
    return plot::Painter::plotArea(area);
}
void WaveformWidget::draw(lv_event_t *event) {
    auto &self = *static_cast<WaveformWidget *>(lv_event_get_user_data(event));
    lv_area_t area;
    lv_obj_get_coords(self.object_, &area);
    plot::Painter(lv_event_get_layer(event), area, self.model_, self.palette_, self.inlineScales_).draw();
}
void WaveformWidget::cover(lv_event_t *event) {
    auto *info = static_cast<lv_cover_check_info_t *>(lv_event_get_param(event));
    auto *object = static_cast<lv_obj_t *>(lv_event_get_current_target(event));
    if (!info || !object || info->res == LV_COVER_RES_MASKED) return; // A mask found before stays.
    lv_area_t frame;
    lv_obj_get_coords(object, &frame);
    const lv_area_t &a = *info->area;
    const bool inside = a.x1 >= frame.x1 && a.y1 >= frame.y1 && a.x2 <= frame.x2 && a.y2 <= frame.y2;
    const bool plain = lv_obj_get_style_opa(object, LV_PART_MAIN) >= LV_OPA_MAX && lv_obj_get_style_radius(object, LV_PART_MAIN) == 0 &&
                       lv_obj_get_style_transform_width(object, LV_PART_MAIN) == 0 && lv_obj_get_style_transform_height(object, LV_PART_MAIN) == 0 &&
                       lv_obj_get_style_transform_scale_x(object, LV_PART_MAIN) == LV_SCALE_NONE &&
                       lv_obj_get_style_transform_scale_y(object, LV_PART_MAIN) == LV_SCALE_NONE &&
                       lv_obj_get_style_transform_rotation(object, LV_PART_MAIN) == 0 &&
                       lv_obj_get_style_blend_mode(object, LV_PART_MAIN) == LV_BLEND_MODE_NORMAL;
    if (inside && plain) info->res = LV_COVER_RES_COVER; // Else LVGL's own (not covered: no background).
}
void WaveformWidget::geometry(lv_event_t *event) {
    auto &self = *static_cast<WaveformWidget *>(lv_event_get_user_data(event));
    if (lv_event_get_code(event) == LV_EVENT_DELETE)
        self.object_ = self.note_ = self.counter_ = nullptr;
    else {
        self.model_.clear();
        lv_obj_invalidate(self.object_);
    }
}
} // namespace openosc
