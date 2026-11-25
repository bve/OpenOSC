#include "openosc/ui/components/record_overview.hpp"
#include "openosc/ui/scope/format.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::ui {
namespace {
lv_obj_t *box(lv_obj_t *parent, uint32_t color) {
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(object, false);
    lv_obj_set_hidden(object, true);
    return object;
}
// Filled arrows at the strip's ends when the view lies wholly beyond it, as 4x7 alpha maps.
constexpr uint8_t BeforeAlpha[28] = {
      0,   0,  32, 255,
      0,  32, 223, 255,
     32, 223, 255, 255,
    255, 255, 255, 255,
     32, 223, 255, 255,
      0,  32, 223, 255,
      0,   0,  32, 255,
};
constexpr uint8_t AfterAlpha[28] = {
    255,  32,   0,   0,
    255, 223,  32,   0,
    255, 255, 223,  32,
    255, 255, 255, 255,
    255, 255, 223,  32,
    255, 223,  32,   0,
    255,  32,   0,   0,
};
lv_image_dsc_t arrowImage(const uint8_t *alpha) {
    lv_image_dsc_t image{};
    image.header.magic = LV_IMAGE_HEADER_MAGIC;
    image.header.cf = LV_COLOR_FORMAT_A8;
    image.header.w = 4; image.header.h = 7; image.header.stride = 4;
    image.data_size = sizeof BeforeAlpha;
    image.data = alpha;
    return image;
}
const lv_image_dsc_t BeforeImage = arrowImage(BeforeAlpha), AfterImage = arrowImage(AfterAlpha);
lv_obj_t *arrow(lv_obj_t *parent, const lv_image_dsc_t *image, int x, uint32_t color) {
    auto *object = lv_image_create(parent);
    lv_image_set_src(object, image);
    lv_obj_set_pos(object, x, 1 + (RecordOverview::InsideHeight - 7) / 2);
    lv_obj_set_style_image_recolor(object, lv_color_hex(color), 0);
    lv_obj_set_style_image_recolor_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_hidden(object, true);
    return object;
}
// Pixels [left, left + width) of a span of points [begin, end) in Q16, at least the 2-pixel
// minimum, kept inside the strip.
void span(int64_t begin, int64_t end, int64_t length, int inside, int &left, int &width) {
    left = int(begin * inside / length);
    const int right = int((end * inside + length - 1) / length);
    width = std::min(inside, std::max(right - left, OverviewGeometry::MinimumSpan));
    left = std::clamp(left, 0, inside - width);
}
} // namespace

OverviewGeometry OverviewGeometry::calculate(const scope::FrameMetadata &m, const plot::Viewport &view, plot::Mode mode,
                                             uint32_t analyzedPoints, int inside) {
    OverviewGeometry g;
    if (!m.valid() || inside < MinimumSpan) return g;
    g.points = m.samples * ((m.channels[0].enabled ? 1U : 0U) + (m.channels[1].enabled ? 1U : 0U));
    g.budget = uint32_t(m.budgetK) * 1024;
    g.budgetShort = m.budgetShort;
    // Each point owns one unit of the record, so its last point still has a width.
    const int64_t length = int64_t(m.samples) << 16, last = int64_t(m.samples - 1) << 16;
    if (mode != plot::Mode::Spectrum && view.spanQ16) {
        const int64_t first = view.startQ16, end = view.startQ16 + int64_t(view.spanQ16);
        if (end < 0) g.before = true;
        else if (first > last) g.after = true;
        else span(std::max<int64_t>(first, 0), std::min(end, last) + 65536, length, inside, g.window, g.windowWidth);
    }
    if ((mode == plot::Mode::Spectrum || mode == plot::Mode::SpectrumOverlay) && analyzedPoints)
        span(0, int64_t(std::min(analyzedPoints, m.samples)) << 16, length, inside, g.analyzed, g.analyzedWidth);
    if (m.triggered && !m.rolling && m.triggerIndex < m.samples)
        g.trigger = std::min(inside - 1, int(((int64_t(m.triggerIndex) << 16) + 32768) * inside / length));
    return g;
}
OverviewGeometry OverviewGeometry::calculate(const plot::Model &model, int inside) {
    if (!model.hasFrame()) return {};
    const auto &spectrum = model.spectrum();
    return calculate(model.metadata(), model.viewport(), model.options().mode,
                     spectrum.valid ? spectrum.count * spectrum.decimation : 0, inside);
}
void RecordOverview::clear() {
    if (root_) lv_obj_delete(root_);
    root_ = caption_ = map_ = nullptr;
    for (auto &part : parts_) part = nullptr;
    shown_ = {}; split_ = drawn_ = false; analyzedColor_ = 0;
}
void RecordOverview::create(lv_obj_t *parent, const Theme &theme) {
    clear();
    channels_[0] = theme.channel1; channels_[1] = theme.channel2; math_ = theme.math;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, Layout::Overview.x, Top);
    lv_obj_set_size(root_, Layout::Overview.width, MapTop + 14);
    lv_obj_set_scrollable(root_, false);
    caption_ = Elements::label(root_, "--", 0, MapTop);
    lv_obj_set_size(caption_, Layout::OverviewCaption.width, 14);
    lv_obj_set_style_text_font(caption_, Elements::font(12), 0);
    lv_obj_set_style_text_align(caption_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(caption_, lv_color_hex(theme.muted), 0);
    // The outline is the map's own background showing around the record body.
    const int mapX = Layout::OverviewMap.x - Layout::Overview.x;
    map_ = box(root_, theme.muted);
    lv_obj_set_hidden(map_, false);
    lv_obj_set_pos(map_, mapX, MapTop);
    lv_obj_set_size(map_, Layout::OverviewMap.width, Height);
    auto *body = box(map_, theme.panel);
    lv_obj_set_hidden(body, false);
    lv_obj_set_pos(body, 1, 1); lv_obj_set_size(body, Inside, InsideHeight);
    parts_[Window] = box(map_, theme.accent);
    // The FFT's span: a bracket open at the top, so it never reads as the window on screen.
    parts_[Analyzed] = box(map_, theme.panel);
    lv_obj_set_style_bg_opa(parts_[Analyzed], LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(parts_[Analyzed], 1, 0);
    lv_obj_set_style_border_side(parts_[Analyzed], lv_border_side_t(LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT | LV_BORDER_SIDE_BOTTOM), 0);
    parts_[Before] = arrow(map_, &BeforeImage, 2, theme.accent);
    parts_[After] = arrow(map_, &AfterImage, 1 + Inside - 5, theme.accent);
    // The trigger: a T whose head sits above the strip and whose stem crosses it.
    parts_[TriggerStem] = box(map_, theme.foreground);
    lv_obj_set_size(parts_[TriggerStem], 1, InsideHeight);
    parts_[TriggerHead] = box(root_, theme.foreground);
    lv_obj_set_size(parts_[TriggerHead], 3, MapTop);
}
void RecordOverview::palette(const Theme &theme) {
    if (!root_) return;
    channels_[0] = theme.channel1; channels_[1] = theme.channel2; math_ = theme.math;
    const auto bg = [](lv_obj_t *o, uint32_t c) { if (o) lv_obj_set_style_bg_color(o, lv_color_hex(c), 0); };
    lv_obj_set_style_text_color(caption_, lv_color_hex(theme.muted), 0);
    bg(map_, theme.muted);
    bg(lv_obj_get_child(map_, 0), theme.panel); // The record body.
    bg(parts_[Window], theme.accent);
    bg(parts_[TriggerStem], theme.foreground);
    bg(parts_[TriggerHead], theme.foreground);
    for (auto *a : {parts_[Before], parts_[After]}) lv_obj_set_style_image_recolor(a, lv_color_hex(theme.accent), 0);
    analyzedColor_ = 0; drawn_ = false; // The analysed span and the trigger's colour: on the next update.
}
void RecordOverview::update(const plot::Model &model, const plot::ChannelView &view) {
    if (!root_) return;
    const auto geometry = OverviewGeometry::calculate(model, Inside);
    const auto &options = model.options();
    const bool split = model.hasFrame() && options.mode == plot::Mode::SpectrumOverlay;
    // The bracket takes the colour of the FFT trace it stands for.
    const uint32_t analyzed = split ? math_ : channels_[view.slot(options.spectrum.channel < 2 ? options.spectrum.channel : 0)];
    if (drawn_ && geometry == shown_ && split == split_ && analyzed == analyzedColor_) return;
    show(geometry, split, analyzed);
}
// The budget stays the caption whatever the rate (a stable setting, GitHub #131); "<" says the capture plan
// reserves part of it (#138: FrameMetadata::budgetShort); the record's exact length is Captured's.
void RecordOverview::captionText(const OverviewGeometry &g, char *output, size_t capacity) {
    if (!output || !capacity) return;
    if (!g.points) { std::snprintf(output, capacity, "--"); return; } // No frame on show.
    if (!g.budget || g.points > g.budget) { plot::Format::compactPoints(g.points, output, capacity); return; }
    char total[8];
    plot::Format::compactPoints(g.budget, total, sizeof total);
    // GitHub #138: "<" is the capture plan's: it reserves part of the budget (every record of it, held too),
    // never one record's own length, which a freeze in flight varies by a few points (Captured is exact).
    std::snprintf(output, capacity, "%s%s", g.budgetShort ? "<" : "", total);
}
void RecordOverview::show(const OverviewGeometry &g, bool split, uint32_t analyzedColor) {
    drawn_ = true;
    char text[12];
    captionText(g, text, sizeof text);
    Elements::setText(caption_, text); // One font, IBM Plex Sans 12, in every state (GitHub #135).
    const auto place = [](lv_obj_t *object, int left, int width, int y, int height) {
        lv_obj_set_hidden(object, left < 0);
        if (left < 0) return;
        lv_obj_set_pos(object, 1 + left, 1 + y);
        lv_obj_set_size(object, width, height);
    };
    place(parts_[Window], g.window, g.windowWidth, 0, split ? SplitWindow : InsideHeight);
    if (analyzedColor != analyzedColor_) lv_obj_set_style_border_color(parts_[Analyzed], lv_color_hex(analyzedColor), 0);
    place(parts_[Analyzed], g.analyzed, g.analyzedWidth, AnalyzedTop, AnalyzedRows);
    lv_obj_set_hidden(parts_[Before], !g.before);
    lv_obj_set_hidden(parts_[After], !g.after);
    for (unsigned p : {TriggerStem, TriggerHead}) lv_obj_set_hidden(parts_[p], g.trigger < 0);
    if (g.trigger >= 0) {
        lv_obj_set_pos(parts_[TriggerStem], 1 + g.trigger, 1);
        lv_obj_set_pos(parts_[TriggerHead], Layout::OverviewMap.x - Layout::Overview.x + g.trigger, 0); // Centred on the stem.
    }
    lv_obj_move_foreground(parts_[TriggerStem]);
    shown_ = g; split_ = split; analyzedColor_ = analyzedColor;
}
} // namespace openosc::ui
