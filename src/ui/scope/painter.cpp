#include "openosc/ui/scope/painter.hpp"
#include <algorithm>
#include "openosc/ui/components/elements.hpp"

namespace openosc::plot {
void Painter::draw() {
    const lv_area_t saved = layer_->_clip_area;
    const auto within = [&](const lv_area_t &a) {
        return lv_area_t{std::max(saved.x1, a.x1), std::max(saved.y1, a.y1), std::min(saved.x2, a.x2), std::min(saved.y2, a.y2)};
    };
    const auto empty = [](const lv_area_t &a) { return a.x1 > a.x2 || a.y1 > a.y2; };
    // One clear of the whole waveform frame: the plot and the marker margins, including a dirty
    // area confined to a margin (the parent may have another background, and old marker pixels
    // must disappear). The plot needs no second fill of its own.
    const lv_area_t clipped = within(area_), margins = within(frame_);
    if (empty(margins)) return;
    layer_->_clip_area = margins;
    fill(frame_, palette_.background);
    const bool plot = !empty(clipped), spectrumOnly = model_.options().mode == Mode::Spectrum;
    if (plot) {
        layer_->_clip_area = clipped;
        model_.statistics().drawBatches = 0;
        // Time traces only: X-Y and the spectrum have no time axis to run off the record.
        const auto mode = model_.options().mode;
        const RecordGaps gaps = model_.hasFrame() && (mode == Mode::Time || mode == Mode::SpectrumOverlay)
            ? RecordGaps::of(model_.viewport(), model_.metadata().samples) : RecordGaps{};
        recordGaps(gaps);
        grid();
        if (!model_.hasFrame())
            text("NO DATA", std::max<int>(2, (lv_area_get_width(&area_) - 60) / 2),
                 lv_area_get_height(&area_) / 2 - 6, palette_.readout);
        else if (spectrumOnly) spectrum();
        else traces();
        // The overlay's spectrum spans the whole width over its own frequency axis: the words
        // would read as if it ended there, so it keeps only the tint and the edge.
        if (mode == Mode::Time) recordGapLabels(gaps);
    }
    if (model_.hasFrame() && !spectrumOnly) {
        // The markers live in the margins around the plot (their dashed guides inside it).
        if (!empty(margins)) { layer_->_clip_area = margins; markers(); }
        if (plot) {
            layer_->_clip_area = clipped;
            cursors();
            if (inlineScales_) scales();
            if (model_.options().mode == Mode::SpectrumOverlay) spectrum();
        }
    }
    layer_->_clip_area = saved;
}
uint32_t Painter::traceColor(unsigned channel) const {
    return ColorShade::toward(palette_.channel(channel), palette_.background, model_.options().channels[channel & 1].brightness);
}
void Painter::fill(const lv_area_t &box, uint32_t color) {
    lv_draw_rect_dsc_t rectangle;
    lv_draw_rect_dsc_init(&rectangle);
    rectangle.bg_color = lv_color_hex(color);
    rectangle.bg_opa = LV_OPA_COVER;
    lv_draw_rect(layer_, &rectangle, &box);
}
void Painter::tag(const char *value, int x, int y, uint32_t foreground, uint32_t background) {
    const int width = ui::Elements::textWidth(value) + 4;
    const lv_area_t box = {area_.x1 + x, area_.y1 + y, area_.x1 + x + width - 1, area_.y1 + y + 13};
    fill(box, background);
    text(value, x + 2, y, foreground, width - 2);
}
void Painter::line(int x1, int y1, int x2, int y2, uint32_t color, uint8_t opacity, bool dashed, unsigned width) {
    lv_draw_line_dsc_t descriptor;
    lv_draw_line_dsc_init(&descriptor);
    descriptor.color = lv_color_hex(color);
    descriptor.opa = opacity;
    descriptor.raw_end = true;
    descriptor.width = lv_value_precise_t(width);
    descriptor.p1 = {lv_value_precise_t(area_.x1 + x1), lv_value_precise_t(area_.y1 + y1)};
    descriptor.p2 = {lv_value_precise_t(area_.x1 + x2), lv_value_precise_t(area_.y1 + y2)};
    if (dashed) {
        descriptor.dash_width = 3;
        descriptor.dash_gap = 3;
    }
    lv_draw_line(layer_, &descriptor);
    ++model_.statistics().drawBatches;
}
void Painter::text(const char *value, int x, int y, uint32_t color, int width,bool solid) {
    lv_area_t box = {area_.x1 + x, area_.y1 + y, area_.x1 + x + width - 1, area_.y1 + y + 13};
    if (solid) fill(box, palette_.background);
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.font = ui::Elements::font(12);
    descriptor.color = lv_color_hex(color);
    descriptor.text = value;
    descriptor.text_local = 1;
    lv_draw_label(layer_, &descriptor, &box);
}
void Painter::triangle(Point a, Point b, Point c, uint32_t color) {
    lv_draw_triangle_dsc_t d;
    lv_draw_triangle_dsc_init(&d);
    d.color = lv_color_hex(color);
    const Point points[] = {a, b, c};
    for (unsigned i = 0; i < 3; ++i)
        d.p[i] = {lv_value_precise_t(area_.x1 + points[i].x), lv_value_precise_t(area_.y1 + points[i].y)};
    lv_draw_triangle(layer_, &d);
}
} // namespace openosc::plot
