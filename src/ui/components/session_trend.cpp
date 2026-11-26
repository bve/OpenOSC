#include "openosc/ui/components/session_trend.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/measurement/meter_function.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/editors/meter_log_properties.hpp"
#include "openosc/ui/scope/format.hpp"
#include <cmath>
#include <cstdio>

namespace openosc::ui {
namespace {
constexpr const char *NoValue = "--";
void text(lv_layer_t *layer, const char *s, int x, int y, int width, uint32_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = Elements::font(12);
    d.color = lv_color_hex(color);
    d.text = s; d.text_local = 1; d.align = align;
    lv_area_t a{lv_coord_t(x), lv_coord_t(y), lv_coord_t(x + width - 1), lv_coord_t(y + 13)};
    lv_draw_label(layer, &d, &a);
}
void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color, int dash = 0) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(color); d.width = 1;
    if (dash) { d.dash_width = lv_value_precise_t(dash); d.dash_gap = lv_value_precise_t(dash); }
    d.p1 = {lv_value_precise_t(x1), lv_value_precise_t(y1)}; d.p2 = {lv_value_precise_t(x2), lv_value_precise_t(y2)};
    lv_draw_line(layer, &d);
}
void box(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    lv_area_t a{lv_coord_t(x1), lv_coord_t(y1), lv_coord_t(x2), lv_coord_t(y2)};
    lv_draw_rect(layer, &d, &a);
}
// The scale: the values' envelope with a margin, never of no height (a flat session sits mid-plot).
bool scale(const dmm::TrendWindow *trend, double &low, double &high) {
    if (!trend || !trend->range(low, high)) return false;
    double span = high - low;
    if (!(span > 0)) span = std::fabs(high) > 0 ? std::fabs(high) * 1e-3 : 1e-3;
    low -= span * 0.05;
    high += span * 0.05;
    if (!(high > low)) high = low + 1e-9;
    return true;
}
} // namespace
void SessionTrendView::value(double si, Quantity quantity, char *output, size_t capacity) {
    if (!capacity) return;
    output[0] = 0;
    if (!std::isfinite(si)) return;
    if (quantity == Quantity::Temperature) {
        char digits[24];
        if (Decimal::fixed(si, 2, digits, sizeof digits)) std::snprintf(output, capacity, "%s \xc2\xb0" "C", digits);
        return;
    }
    plot::Format::quantity(si, dmm::LogContext::siUnit(quantity), output, capacity, 5);
}
void SessionTrendView::time(uint64_t us, bool fine, char *output, size_t capacity) {
    const uint64_t s = us / 1000000;
    const auto u = [](uint64_t v) { return static_cast<unsigned long>(v); };
    if (s >= 3600) std::snprintf(output, capacity, "%lu:%02lu:%02lu", u(s / 3600), u(s / 60 % 60), u(s % 60));
    else if (fine) std::snprintf(output, capacity, "%lu:%02lu.%lu", u(s / 60), u(s % 60), u(us / 100000 % 10));
    else std::snprintf(output, capacity, "%lu:%02lu", u(s / 60), u(s % 60));
}
int SessionTrendView::y(double v, double low, double high) {
    const double f = (v - low) / (high - low);
    const double clamped = f < 0 ? 0 : f > 1 ? 1 : f;
    return PlotBottom - int(std::lround(clamped * (PlotHeight - 1)));
}
void SessionTrendView::create(lv_obj_t *parent, const MenuPalette &palette) {
    palette_ = palette;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, Top);
    lv_obj_set_size(root_, Width, Height);
    lv_obj_set_scrollable(root_, false);
    lv_obj_add_event_cb(root_, draw, LV_EVENT_DRAW_MAIN, this);
}
void SessionTrendView::show(const SessionTrendData &data) {
    data_ = data;
    if (root_) lv_obj_invalidate(root_);
}
void SessionTrendView::draw(lv_event_t *event) {
    auto &self = *static_cast<SessionTrendView *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    lv_area_t area;
    lv_obj_get_coords(self.root_, &area);
    const int x0 = area.x1, y0 = area.y1;
    self.summary(layer, x0, y0);
    double low = 0, high = 1;
    const bool values = scale(self.data_.trend, low, high);
    self.plot(layer, x0, y0, low, high);
    self.axis(layer, x0, y0, low, high);
    if (values) self.readout(layer, x0, y0);
    if (self.data_.status) {
        const int w = Elements::textWidth(self.data_.status, 12) + 12;
        const int cx = x0 + (PlotLeft + PlotRight) / 2, cy = y0 + PlotTop + PlotHeight / 2 - 8;
        box(layer, cx - w / 2, cy, cx + w / 2, cy + 16, self.palette_.selection);
        text(layer, self.data_.status, cx - w / 2, cy + 1, w, self.palette_.text, LV_TEXT_ALIGN_CENTER);
    }
}
void SessionTrendView::summary(lv_layer_t *layer, int x0, int y0) const {
    char line[160], span[24], interval[16]; // Room for every part: the label cuts what does not fit.
    const auto *info = data_.info;
    if (!info) return;
    const char *function = unsigned(info->context.function) < MeterFunctionCount ? MeterFunctions[unsigned(info->context.function)].name : "?";
    MeterLogProperties::clock(data_.lastUs > data_.firstUs ? data_.lastUs - data_.firstUs : 0, span, sizeof span);
    MeterLogProperties::span(info->settings.intervalS(), interval, sizeof interval);
    char band[16];
    MeterLogProperties::percent(info->settings.thresholdPermille(), band, sizeof band);
    const char *end = info->ended ? dmm::logEndKey(info->summary.end) : "unfinished";
    char skipped[20] = "";
    if (data_.invalid) std::snprintf(skipped, sizeof skipped, "  %lu bad", static_cast<unsigned long>(data_.invalid));
    std::snprintf(line, sizeof line, "%s %s  %s  %s  %lu rec  %s%s", info->settings.mode() == dmm::LogMode::Event ? "Event" : "Interval",
                  info->settings.mode() == dmm::LogMode::Event ? band : interval, function, span,
                  static_cast<unsigned long>(data_.records), end, skipped);
    text(layer, line, x0 + 6, y0, Width - 12, palette_.secondary);
}
// Column by column: a stroke from min to max, widened to meet the column before (a ramp stays one
// line); a column without a value breaks the line. OL marks the top, a gap the baseline.
void SessionTrendView::plot(lv_layer_t *layer, int x0, int y0, double low, double high) const {
    const int left = x0 + PlotLeft, top = y0 + PlotTop, bottom = y0 + PlotBottom, right = x0 + PlotRight;
    line(layer, left - 1, top - 1, right + 1, top - 1, palette_.frame);
    line(layer, left - 1, bottom + 1, right + 1, bottom + 1, palette_.frame);
    line(layer, left - 1, top - 1, left - 1, bottom + 1, palette_.frame);
    line(layer, right + 1, top - 1, right + 1, bottom + 1, palette_.frame);
    for (int q = 1; q < 4; ++q) line(layer, left, top + q * PlotHeight / 4, right, top + q * PlotHeight / 4, palette_.track, 2);
    const auto *trend = data_.trend;
    if (!trend) return;
    int previousLow = 0, previousHigh = 0;
    bool previous = false;
    for (unsigned c = 0; c < dmm::TrendWindow::Columns; ++c) {
        const auto &column = trend->column(c);
        const int x = left + int(c);
        if (column.flags & dmm::TrendColumn::Overload) box(layer, x, top, x, top + 2, palette_.accent);
        if (column.flags & dmm::TrendColumn::Gap) box(layer, x, bottom - 2, x, bottom, palette_.secondary);
        if (!column.value()) { previous = false; continue; }
        const int ys = y(column.maximum, low, high), ye = y(column.minimum, low, high);
        int yHigh = ys, yLow = ye;
        if (previous) {
            if (yHigh > previousLow) yHigh = previousLow; // Meets the column before.
            if (yLow < previousHigh) yLow = previousHigh;
        }
        box(layer, x, y0 + yHigh, x, y0 + yLow, palette_.text);
        previousLow = ye;
        previousHigh = ys;
        previous = true;
    }
    const int cursor = left + int(data_.cursor < dmm::TrendWindow::Columns ? data_.cursor : 0);
    line(layer, cursor, top, cursor, bottom, palette_.accent, 2);
}
// The scale's limits at the left; under the plot the window's start and end, the zoom, and the
// window's place in the session (a bar under the times).
void SessionTrendView::axis(lv_layer_t *layer, int x0, int y0, double low, double high) const {
    char a[24], b[24];
    const Quantity quantity = data_.info ? data_.info->context.quantity : Quantity::Unknown;
    if (data_.trend && data_.trend->range(low, high) ) {
        double l = 0, h = 0;
        scale(data_.trend, l, h);
        value(h, quantity, a, sizeof a);
        value(l, quantity, b, sizeof b);
        text(layer, a, x0, y0 + PlotTop - 2, PlotLeft - 3, palette_.secondary, LV_TEXT_ALIGN_RIGHT);
        text(layer, b, x0, y0 + PlotBottom - 12, PlotLeft - 3, palette_.secondary, LV_TEXT_ALIGN_RIGHT);
    }
    if (!data_.trend) return;
    const uint64_t start = data_.trend->startUs(), end = data_.trend->endUs();
    const bool fine = end - start < 600000000ULL;
    time(start, fine, a, sizeof a);
    time(end, fine, b, sizeof b);
    const int ty = y0 + PlotBottom + 3;
    text(layer, a, x0 + PlotLeft, ty, 90, palette_.secondary);
    text(layer, b, x0 + PlotRight - 89, ty, 90, palette_.secondary, LV_TEXT_ALIGN_RIGHT);
    char zoom[16];
    std::snprintf(zoom, sizeof zoom, "x%u", data_.zoom);
    text(layer, zoom, x0 + (PlotLeft + PlotRight) / 2 - 30, ty, 60, palette_.secondary, LV_TEXT_ALIGN_CENTER);
    const uint64_t extent = data_.lastUs > data_.firstUs ? data_.lastUs - data_.firstUs : 1;
    const int width = PlotRight - PlotLeft;
    const int from = int((start > data_.firstUs ? start - data_.firstUs : 0) * uint64_t(width) / extent);
    const int to = int((end > data_.firstUs ? end - data_.firstUs : 0) * uint64_t(width) / extent);
    line(layer, x0 + PlotLeft, ty + 15, x0 + PlotRight, ty + 15, palette_.track);
    box(layer, x0 + PlotLeft + from, ty + 14, x0 + PlotLeft + (to > from ? to : from), ty + 16, palette_.accent);
}
// The cursor's column: its start time, then min / max / avg of the readings in it (or why it has none).
void SessionTrendView::readout(lv_layer_t *layer, int x0, int y0) const {
    const auto &trend = *data_.trend;
    const unsigned c = data_.cursor < dmm::TrendWindow::Columns ? data_.cursor : 0;
    const auto &column = trend.column(c);
    const Quantity quantity = data_.info ? data_.info->context.quantity : Quantity::Unknown;
    char at[24], low[24], high[24], average[24], line1[96];
    time(trend.columnStart(c), trend.endUs() - trend.startUs() < 600000000ULL, at, sizeof at);
    const int ry = y0 + PlotBottom + 33;
    if (!column.value()) {
        const char *why = column.flags & dmm::TrendColumn::Overload ? "OL" : column.flags & dmm::TrendColumn::Gap ? "no value" : "no data";
        std::snprintf(line1, sizeof line1, "%s  %s", at, why);
        text(layer, line1, x0 + 6, ry, Width - 12, palette_.text);
        return;
    }
    value(column.minimum, quantity, low, sizeof low);
    value(column.maximum, quantity, high, sizeof high);
    value(column.average(), quantity, average, sizeof average);
    std::snprintf(line1, sizeof line1, "%s  avg %s%s", at, average[0] ? average : NoValue,
                  column.flags & dmm::TrendColumn::Overload ? "  OL" : "");
    text(layer, line1, x0 + 6, ry, Width - 12, palette_.accent);
    std::snprintf(line1, sizeof line1, "min %s  max %s", low, high);
    text(layer, line1, x0 + 6, ry + 13, Width - 12, palette_.text);
}
} // namespace openosc::ui
