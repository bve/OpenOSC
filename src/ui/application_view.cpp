#include "openosc/ui/application_view.hpp"
#include "openosc/ui/components/scope_styles.hpp"
#include <cstdio>
#include <cstring>
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/scope/tag_digits.hpp"
#include "openosc/ui/components/status_bar_layout.hpp"
#include "openosc/ui/components/status_icons.hpp"
#include <algorithm>

namespace openosc {
void ApplicationView::build(const Theme &theme) {
    host_.clear(); battery_.clear();
    if (menu_) lv_obj_delete(menu_);
    menuContent_.reset(); menuRetained_=false; draftPreviewObject_=nullptr;
    if (toast_) lv_obj_delete(toast_);
    if (generatorIndicator_) lv_obj_delete(generatorIndicator_);
    if (usbIndicator_) lv_obj_delete(usbIndicator_);
    if (fps_) lv_obj_delete(fps_);
    if (trigger_) lv_obj_delete(trigger_);
    if (edge_) lv_obj_delete(edge_);
    if (channelTag_) lv_obj_delete(channelTag_);
    channelTag_ = nullptr;
    if (clock_) lv_obj_delete(clock_);
    bode_.clear();
    bodeHeader_ = false;
    clock_ = nullptr; clockInk_ = 0; clockLayout_ = 0; // The icons and the overview below are created at their usual places.
    channelTagChannel_ = NoTriggerChannel;
    overview_.clear();
    powerHold_.theme(theme);
    theme_ = &theme;
    toastActive_ = false;
    auto *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(theme.background), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(theme.foreground), 0);
    lv_obj_set_scrollable(screen, false);
    host_.build(screen, theme);
    applyScopeTheme(false); // The next measurement() activates with it.
    menu_ = lv_obj_create(screen);
    lv_obj_remove_style_all(menu_);
    lv_obj_set_size(menu_, 304, 218);
    lv_obj_set_style_bg_opa(menu_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(menu_, lv_color_hex(theme.background), 0);
    lv_obj_set_style_border_color(menu_, lv_color_hex(theme.grid), 0);
    lv_obj_set_style_border_width(menu_, 1, 0);
    lv_obj_set_style_radius(menu_, 3, 0);
    lv_obj_set_scrollable(menu_, false);
    lv_obj_set_hidden(menu_, true);
    toast_ = label(screen, "", 8, 178);
    lv_obj_set_size(toast_, 304, 40);
    ui::Elements::overlayPanel(toast_, shown());
    lv_obj_set_style_pad_all(toast_, 4, 0);
    lv_obj_set_hidden(toast_, true);
    using Layout = ui::StatusBarLayout;
    generatorIndicator_ = ui::StatusIcons::generator(screen, Layout::Generator.x, 4, theme.generator);
    lv_obj_set_hidden(generatorIndicator_, true);
    generatorOn_ = false;
    usbIndicator_ = ui::StatusIcons::usb(screen, Layout::Usb.x, 4, theme.accent);
    lv_obj_set_hidden(usbIndicator_,true); usbConnected_ = false;
    overview_.create(screen, theme);
    edge_ = lv_line_create(screen);
    lv_obj_set_pos(edge_,Layout::edge(Layout::Trigger).x,5);
    lv_obj_set_style_line_color(edge_,lv_color_hex(theme.muted),0);
    lv_obj_set_style_line_width(edge_,1,0);
    channelTag_ = lv_obj_create(screen);
    lv_obj_remove_style_all(channelTag_);
    lv_obj_set_size(channelTag_,plot::PlotFrame::TagWidth,plot::PlotFrame::TagHeight);
    lv_obj_set_pos(channelTag_,Layout::level(Layout::Trigger).x,5);
    lv_obj_add_event_cb(channelTag_,drawChannelTag,LV_EVENT_DRAW_MAIN,this);
    lv_obj_set_hidden(channelTag_,true);
    clock_ = lv_obj_create(screen); // Nothing of its own but the glyph: no background, no border.
    lv_obj_remove_style_all(clock_);
    lv_obj_set_size(clock_, Layout::ClockWidth, Layout::ClockHeight);
    lv_obj_set_pos(clock_, Layout::ClockBesideIcon.x, Layout::ClockYIcon); // Placed by placeClock().
    lv_obj_add_event_cb(clock_, drawClock, LV_EVENT_DRAW_MAIN, this);
    lv_obj_set_hidden(clock_, true);
    bode_.create(screen, theme); // Over the plot, under the toast (raised when shown).
    trigger_ = label(screen,"",Layout::level(Layout::Trigger).x+plot::PlotFrame::TagWidth+1,4);
    lv_obj_set_size(trigger_,Layout::LevelWidth-plot::PlotFrame::TagWidth-1,14);
    lv_obj_set_style_text_font(trigger_,ui::Elements::font(12),0);
    fps_ = label(screen,"",Layout::Fps.x,4);
    lv_obj_set_size(fps_,Layout::Fps.width,14);
    lv_obj_set_style_text_font(fps_,ui::Elements::font(12),0);
    lv_obj_set_style_text_align(fps_,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_set_style_text_color(fps_,lv_color_hex(theme.muted),0);
    lv_obj_set_hidden(fps_,true); fpsWanted_ = fpsVisible_ = false;
    battery_.create(screen);
    host_.statusX(Layout::StatusX);
    statusRight_ = -1;
}
void ApplicationView::capture(const scope::FrameView &frame, bool held, unsigned channel, DataStatus health) {
    scope_.update(frame, held, channel, health);
    if (!host_.overlayActive()) overview_.update(host_.waveform().model());
    refreshIndicators();
}
void ApplicationView::refreshIndicators() {
    // A state wider than its column hides what it would cover instead of overlapping it. The
    // right side (overview, USB, GEN) lies past the widest state and never gives way.
    using Layout = ui::StatusBarLayout;
    statusRight_ = host_.statusRight();
    const bool scope = mode_ == MeasurementMode::Oscilloscope && !bodeHeader_;
    overview_.visible(scope);
    const bool trigger = scope && trigger_ && edge_ && Layout::trigger(statusRight_);
    if (trigger_) lv_obj_set_hidden(trigger_, !trigger);
    if (edge_) lv_obj_set_hidden(edge_, !trigger);
    if (channelTag_) lv_obj_set_hidden(channelTag_, !trigger || channelTagChannel_ == NoTriggerChannel);
    placeFps();
    placeClock();
}
// The run profile glyph: enabled in the Scope, left of the battery; beside a text battery USB, GEN and
// the overview close up. Moved only when the setting, the mode or the battery display changes (never
// by a reading, a state or the profile); the glyph unless no profile runs. No pill reaches either.
void ApplicationView::placeClock() {
    if (!clock_ || !usbIndicator_ || !generatorIndicator_ || !overview_.root()) return;
    using Layout = ui::StatusBarLayout;
    const bool on = clockEnabled_ && mode_ == MeasurementMode::Oscilloscope;
    const uint8_t layout = !on ? 0 : battery_.text() ? 2 : 1;
    if (layout != clockLayout_) {
        clockLayout_ = layout;
        const bool dense = layout == 2;
        lv_obj_set_x(usbIndicator_, (dense ? Layout::UsbDense : Layout::Usb).x);
        lv_obj_set_x(generatorIndicator_, (dense ? Layout::GeneratorDense : Layout::Generator).x);
        lv_obj_set_x(overview_.root(), (dense ? Layout::OverviewDense : Layout::Overview).x);
        lv_obj_set_pos(clock_, Layout::clock(dense).x, Layout::clockY(dense));
    }
    const bool visible = on && ui::ProfileGlyph::shown(clockMode_);
    if (visible == !lv_obj_is_hidden(clock_)) return;
    lv_obj_set_hidden(clock_, !visible);
    if (visible) lv_obj_move_foreground(clock_);
}
// Its ink on the bar as shown (the Scope's look, light or dark); invalidated only when it changes.
void ApplicationView::paintClock() {
    if (!clock_) return;
    const auto &t = shown();
    const uint32_t ink = ui::ProfileGlyph::color(clockMode_, t.background, t.muted);
    if (ink == clockInk_) return;
    clockInk_ = ink;
    lv_obj_invalidate(clock_);
}
void ApplicationView::clockProfile(bool enabled, uint32_t mode) {
    if (!ui::ProfileGlyph::shown(mode)) mode = RunModeCount;
    if (enabled == clockEnabled_ && mode == clockMode_) return; // Each display pass: nothing to do.
    const bool letter = mode != clockMode_;
    clockEnabled_ = enabled; clockMode_ = mode;
    if (letter && clock_) lv_obj_invalidate(clock_);
    paintClock();
    placeClock();
}
void ApplicationView::drawClock(lv_event_t *event) {
    auto &self = *static_cast<ApplicationView *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(self.clock_, &area);
    lv_draw_rect_dsc_t ink;
    lv_draw_rect_dsc_init(&ink);
    ink.bg_color = lv_color_hex(self.clockInk_);
    using Glyph = ui::ProfileGlyph;
    for (int y = 0; y < Glyph::Height; ++y)
        for (int x = 0; x < Glyph::Width;) {
            if (!Glyph::ink(self.clockMode_, x, y)) { ++x; continue; }
            const int start = x;
            while (x < Glyph::Width && Glyph::ink(self.clockMode_, x, y)) ++x;
            lv_area_t run = {lv_coord_t(area.x1 + start), lv_coord_t(area.y1 + y), lv_coord_t(area.x1 + x - 1), lv_coord_t(area.y1 + y)};
            lv_draw_rect(layer, &ink, &run);
        }
}
// The counter has its own column; only a pill wide enough to reach it hides it.
void ApplicationView::placeFps(bool force) {
    using Layout = ui::StatusBarLayout;
    if (!fps_ || !trigger_) return; // Not built yet.
    const bool visible = fpsWanted_ && Layout::clear(statusRight_, Layout::Fps) && !bodeHeader_;
    if (!force && visible == fpsVisible_) return;
    fpsVisible_ = visible;
    lv_obj_set_hidden(fps_, !visible);
    if (visible) lv_obj_move_foreground(fps_);
}
void ApplicationView::decoderResults(const ui::DecodeTable::Content &table, const char *info, const decoding::ResultSet *traced) {
    auto &t = host_.decodeTable();
    if (t.visible()) t.show(table, info);
    auto &strip = host_.decodeStrip();
    if (strip.visible()) strip.show(traced, table.sets, table.revision, host_.waveform().model().viewport());
}
void ApplicationView::batteryDisplay(uint32_t mode) {
    battery_.display(mode);
    refreshIndicators();
}
void ApplicationView::drawChannelTag(lv_event_t *event) {
    auto &self = *static_cast<ApplicationView *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(self.channelTag_, &area);
    lv_draw_rect_dsc_t box, ink;
    lv_draw_rect_dsc_init(&box); lv_draw_rect_dsc_init(&ink);
    box.bg_color = lv_color_hex(self.channelTagColor_);
    ink.bg_color = lv_color_hex(self.channelTagInk_);
    lv_draw_rect(layer, &box, &area);
    const int x0 = area.x1 + plot::TagDigits::Left, y0 = area.y1 + plot::TagDigits::Top;
    for (int y = 0; y < plot::TagDigits::Height; ++y)
        for (int x = 0; x < plot::TagDigits::Width;) {
            if (!plot::TagDigits::ink(self.channelTagChannel_, x, y)) { ++x; continue; }
            const int start = x;
            while (x < plot::TagDigits::Width && plot::TagDigits::ink(self.channelTagChannel_, x, y)) ++x;
            lv_area_t run = {lv_coord_t(x0 + start), lv_coord_t(y0 + y), lv_coord_t(x0 + x - 1), lv_coord_t(y0 + y)};
            lv_draw_rect(layer, &ink, &run);
        }
}
void ApplicationView::refreshTriggerColors(unsigned channel) {
    if (!channelTag_ || channel >= NoTriggerChannel) return;
    const uint32_t color = scopeLook_.trigger(channel ? scopeTheme_.channel2 : scopeTheme_.channel1);
    const uint32_t ink = scopeLook_.tagText(color);
    ui::Elements::textColor(trigger_, color);
    if (channel == channelTagChannel_ && color == channelTagColor_ && ink == channelTagInk_) return;
    const bool first = channelTagChannel_ == NoTriggerChannel;
    channelTagChannel_ = channel;
    channelTagColor_ = color;
    channelTagInk_ = ink;
    if (first) lv_obj_set_hidden(channelTag_, lv_obj_is_hidden(trigger_));
    lv_obj_invalidate(channelTag_);
}
void ApplicationView::scopeTrigger(const scope::TriggerSettings &trigger, const scope::ChannelMetadata &channel) {
    char level[24];
    if (!channel.calibrated || !channel.probeDenominator ||
        !plot::Format::quantity((trigger.level - channel.zeroCode) * channel.voltsPerCode * channel.probeNumerator / channel.probeDenominator, "V", level, sizeof level, 3))
        std::snprintf(level, sizeof level, "%d", trigger.level);
    if (!trigger_ || !edge_) return;
    if (std::strcmp(lv_label_get_text(trigger_), level)) { lv_label_set_text(trigger_, level); placeFps(); }
    refreshTriggerColors(unsigned(trigger.channel));
    static const lv_point_precise_t rising[] = {{0, 9}, {3, 9}, {3, 1}, {7, 1}}, falling[] = {{0, 1}, {3, 1}, {3, 9}, {7, 9}};
    const auto *points = trigger.edge == scope::Edge::Rising ? rising : falling;
    if (lv_line_get_points(edge_) != points) lv_line_set_points(edge_, points, 4);
}
void ApplicationView::fps(bool visible, uint32_t value) {
    char text[20]; std::snprintf(text,sizeof text,"%lu fps",static_cast<unsigned long>(value));
    counter(visible, text);
}
void ApplicationView::counter(bool visible, const char *text) {
    fpsWanted_ = visible;
    const bool changed = fps_ && visible && std::strcmp(lv_label_get_text(fps_), text);
    if (changed) lv_label_set_text(fps_, text);
    placeFps(changed); // Its width changed: placed again.
}
void ApplicationView::scopeAppearance(const ScopeAppearance &appearance) {
    if (!appearance.valid() || appearance == scopeAppearance_) return;
    scopeAppearance_ = appearance;
    if (!theme_) return;
    applyScopeTheme(true);
    paintShell();
}
void ApplicationView::paintShell() {
    const auto &t = shown();
    auto *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(t.background), 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(t.foreground), 0);
    lv_obj_set_style_bg_color(menu_, lv_color_hex(t.background), 0);
    lv_obj_set_style_border_color(menu_, lv_color_hex(t.grid), 0);
    ui::Elements::overlayPanel(toast_, t);
    lv_obj_set_style_image_recolor(usbIndicator_, lv_color_hex(t.accent), 0);
    lv_obj_set_style_image_recolor(generatorIndicator_, lv_color_hex(t.generator), 0);
    lv_obj_set_style_line_color(edge_, lv_color_hex(t.muted), 0);
    lv_obj_set_style_text_color(fps_, lv_color_hex(t.muted), 0);
    refreshTriggerColors(channelTagChannel_);
    overview_.palette(t);
    bode_.colors(t);
    paintClock();
}
// The Scope's theme: the base theme under the appearance's look (ScopeStyles); the host repaints the
// Scope with it when it shows, nothing is rebuilt.
void ApplicationView::applyScopeTheme(bool apply) {
    scopeLook_ = ui::ScopeStyles::look(*theme_, scopeAppearance_);
    ui::ScopeStyles::effective(*theme_, scopeLook_, scopeTheme_);
    host_.scopeTheme(&scopeTheme_, &scopeLook_, apply);
}
void ApplicationView::measurement(const ApplicationState &state) {
    if (unsigned(state.mode) >= InstrumentModeCount) return;
    mode_ = state.mode;
    lv_obj_set_hidden(menu_, true);
    bode_.hide();
    bodeHeader_ = false;
    host_.activate(state.mode);
    paintShell(); // The Scope's appearance or the base theme, by the screen in front.
    ui::InstrumentScreen *screens[] = {&scope_, &meter_, &generator_};
    screens[unsigned(state.mode)]->activate(state);
    refreshIndicators();
}
void ApplicationView::bodePlot(const ui::BodeViewData &data) {
    host_.overlay(true);
    lv_obj_set_hidden(menu_, true);
    if (!bodeHeader_) {
        bodeHeader_ = true;
        host_.status("BODE", ui::StatusTone::Neutral);
        refreshIndicators();
    }
    bode_.show(data);
    if (toastActive_) lv_obj_move_foreground(toast_); // A notice stays over the plot.
}
void ApplicationView::generatorActive(bool enabled) {
    if (generatorOn_ == enabled) return;
    generatorOn_ = enabled;
    lv_obj_set_hidden(generatorIndicator_, !enabled);
    if (enabled) lv_obj_move_foreground(generatorIndicator_);
}
void ApplicationView::usbConnected(bool connected) {
    if (usbConnected_ == connected) return;
    usbConnected_ = connected; lv_obj_set_hidden(usbIndicator_,!connected);
    if (connected) lv_obj_move_foreground(usbIndicator_);
}
void ApplicationView::notify(const char *text, uint32_t now) {
    const uint32_t current=lv_tick_get();
    if (int32_t(current-now)>0) now=current; // A file operation may have outlived the caller's tick.
    lv_label_set_text(toast_, text);
    lv_obj_set_hidden(toast_, false);
    lv_obj_move_foreground(toast_);
    toastUntil_ = now + 3000;
    toastActive_ = true;
}
void ApplicationView::service(uint32_t now) {
    host_.waveform().service(now);
    // The overview follows the projection itself, so HOLD pan and zoom move it without a new frame.
    if (mode_ == MeasurementMode::Oscilloscope && !host_.overlayActive()) overview_.update(host_.waveform().model());
    if (statusRight_ != host_.statusRight()) refreshIndicators(); // Also catches states set by the screens.
    // Panels that refresh themselves must not bury a message the user has yet to read.
    if (toastActive_ && lv_obj_get_index(toast_)+1!=int32_t(lv_obj_get_child_count(lv_obj_get_parent(toast_))))
        lv_obj_move_foreground(toast_);
    if (toastActive_ && int32_t(now - toastUntil_) >= 0) {
        lv_obj_set_hidden(toast_, true);
        toastActive_ = false;
    }
}
} // namespace openosc
