#include "openosc/ui/application_view.hpp"
#include "openosc/ui/components/scope_styles.hpp"
#include <cstdio>
#include <cstring>
#include "openosc/ui/scope/format.hpp"
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
    lv_obj_set_style_bg_color(toast_, lv_color_hex(theme.grid), 0);
    lv_obj_set_style_bg_opa(toast_, LV_OPA_COVER, 0);
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
    trigger_ = label(screen,"",Layout::level(Layout::Trigger).x,4);
    lv_obj_set_size(trigger_,Layout::LevelWidth,14);
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
    const bool scope = mode_ == MeasurementMode::Oscilloscope;
    overview_.visible(scope);
    const bool trigger = scope && trigger_ && edge_ && Layout::trigger(statusRight_);
    if (trigger_) lv_obj_set_hidden(trigger_, !trigger);
    if (edge_) lv_obj_set_hidden(edge_, !trigger);
    placeFps();
}
// The counter has its own column; only a pill wide enough to reach it hides it.
void ApplicationView::placeFps(bool force) {
    using Layout = ui::StatusBarLayout;
    if (!fps_ || !trigger_) return; // Not built yet.
    const bool visible = fpsWanted_ && Layout::clear(statusRight_, Layout::Fps);
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
void ApplicationView::scopeTrigger(const scope::TriggerSettings &trigger, const scope::ChannelMetadata &channel) {
    char level[24], text[40];
    if (!channel.calibrated || !channel.probeDenominator ||
        !plot::Format::quantity((trigger.level - channel.zeroCode) * channel.voltsPerCode * channel.probeNumerator / channel.probeDenominator, "V", level, sizeof level, 3))
        std::snprintf(level, sizeof level, "%d", trigger.level);
    std::snprintf(text, sizeof text, "%u %s", unsigned(trigger.channel) + 1, level); // The colour names the channel.
    if (!trigger_ || !edge_) return;
    if (std::strcmp(lv_label_get_text(trigger_), text)) { lv_label_set_text(trigger_, text); placeFps(); }
    const auto &t = shown();
    ui::Elements::textColor(trigger_, scopeLook_.trigger(trigger.channel == scope::Channel::Ch1 ? t.channel1 : t.channel2));
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
    lv_obj_set_style_bg_color(toast_, lv_color_hex(t.grid), 0);
    lv_obj_set_style_image_recolor(usbIndicator_, lv_color_hex(t.accent), 0);
    lv_obj_set_style_image_recolor(generatorIndicator_, lv_color_hex(t.generator), 0);
    lv_obj_set_style_line_color(edge_, lv_color_hex(t.muted), 0);
    lv_obj_set_style_text_color(fps_, lv_color_hex(t.muted), 0);
    overview_.palette(t);
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
    host_.activate(state.mode);
    paintShell(); // The Scope's appearance or the base theme, by the screen in front.
    ui::InstrumentScreen *screens[] = {&scope_, &meter_, &generator_};
    screens[unsigned(state.mode)]->activate(state);
    refreshIndicators();
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
