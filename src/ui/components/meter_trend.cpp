#include "openosc/ui/components/meter_trend.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/format/meter.hpp"
#include "openosc/data/decimal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <initializer_list>

namespace openosc::ui {
void MeterTrend::create(lv_obj_t *parent, const Theme &theme) {
    root_ = lv_obj_create(parent); lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_,0,22); lv_obj_set_size(root_, lv_pct(100), 30); lv_obj_set_scrollable(root_, false);
    chart_ = lv_chart_create(root_); lv_obj_remove_style_all(chart_);
    lv_obj_set_size(chart_, lv_pct(100), 30);
    lv_obj_set_style_bg_opa(chart_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(chart_, lv_color_hex(theme.background), 0);
    lv_obj_set_style_border_width(chart_, 1, 0);
    lv_obj_set_style_border_color(chart_, lv_color_hex(theme.grid), 0);
    lv_obj_set_style_line_color(chart_, lv_color_hex(theme.grid), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart_, 1, LV_PART_ITEMS);
    lv_obj_set_style_width(chart_, 0, LV_PART_INDICATOR);
    lv_obj_set_style_height(chart_, 0, LV_PART_INDICATOR);
    lv_chart_set_type(chart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart_, dmm::History::Capacity);
    lv_chart_set_axis_range(chart_, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart_, 3, 6);
    series_ = lv_chart_add_series(chart_, lv_color_hex(theme.accent), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_series_ext_y_array(chart_, series_, values_);
    range_ = Elements::label(root_, "", 0, 58);
    reference_ = Elements::label(root_, "", 0, 74);
    for (auto *label : {range_, reference_}) {
        lv_obj_set_size(label, lv_pct(100), 14);
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    }
    lv_obj_set_hidden(range_,true); lv_obj_set_hidden(reference_,true);
    lv_obj_set_style_text_color(reference_, lv_color_hex(theme.muted), 0);
}
void MeterTrend::palette(const MeterLook &look) {
    const auto &palette = look.palette;
    const auto set = [&](lv_color_t current, uint32_t wanted, auto apply) { if (!lv_color_eq(current, lv_color_hex(wanted))) apply(lv_color_hex(wanted)); };
    set(lv_obj_get_style_bg_color(chart_, LV_PART_MAIN), look.color(MeterRole::Background), [&](lv_color_t c) { lv_obj_set_style_bg_color(chart_, c, 0); });
    set(lv_obj_get_style_border_color(chart_, LV_PART_MAIN), palette.line, [&](lv_color_t c) {
        lv_obj_set_style_border_color(chart_, c, 0); lv_obj_set_style_line_color(chart_, c, LV_PART_MAIN); });
    const auto line = lv_color_hex(look.color(MeterRole::Bar));
    if (!lv_color_eq(lv_chart_get_series_color(chart_, series_), line)) lv_chart_set_series_color(chart_, series_, line);
    Elements::textColor(reference_, palette.muted);
}
void MeterTrend::update(const dmm::History *history, Quantity quantity, double reference, bool relative,
                        bool peerRelative, const char *temperatureUnit) {
    int32_t values[dmm::History::Capacity];
    std::fill(std::begin(values), std::end(values), LV_CHART_POINT_NONE);
    const auto commit = [&] {
        if (std::equal(std::begin(values), std::end(values), std::begin(values_))) return;
        std::copy(std::begin(values), std::end(values), std::begin(values_));
        lv_chart_refresh(chart_);
    };
    const unsigned count = history ? history->size() : 0;
    if (count < 2) {
        Elements::setText(range_, "Need two valid samples"); Elements::setText(reference_, "");
        commit(); return;
    }
    double first; history->value(0, first);
    double minimum = first, maximum = first;
    for (unsigned i = 1; i < count; ++i) {
        double value; history->value(i, value);
        minimum = std::min(minimum, value); maximum = std::max(maximum, value);
    }
    const double span = maximum - minimum;
    for (unsigned i = 0; i < count; ++i) {
        double value; history->value(i, value);
        const double normalized = span > 0 && std::isfinite(span) ? (value - minimum) / span : .5;
        values[dmm::History::Capacity - count + i] = int(std::clamp(normalized, 0.0, 1.0) * 1000);
    }
    static constexpr const char *units[] = {"", "V", "A", "Ohm", "F", "Hz"};
    const char *unit = unsigned(quantity) < 6 ? units[unsigned(quantity)] : "";
    char low[32], high[32], text[112];
    const auto format = [&](double value, bool delta, char (&output)[32]) {
        if (quantity != Quantity::Temperature) {
            plot::Format::quantity(value, unit, output, sizeof output, 6);
            return;
        }
        char digits[24]{};
        Decimal::fixed(dmm::TypeK::display(value, temperatureUnit, delta), 1, digits, sizeof digits);
        std::snprintf(output, sizeof output, "%s%s", digits, MeterText::unit(temperatureUnit));
    };
    format(minimum - (relative ? reference : 0), relative || peerRelative, low);
    format(maximum - (relative ? reference : 0), relative || peerRelative, high);
    std::snprintf(text, sizeof text, "%s .. %s / AUTO SCALE", low, high); Elements::setText(range_, text);
    if (relative) {
        format(reference, false, low);
        std::snprintf(text, sizeof text, "REL reference %s", low);
    } else if (peerRelative) std::snprintf(text, sizeof text, "REL reference: controller");
    else std::snprintf(text, sizeof text, "%u samples / session history", count);
    Elements::setText(reference_, text);
    commit();
}
} // namespace openosc::ui
