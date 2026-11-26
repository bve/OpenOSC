#include "openosc/ui/components/meter_summary.hpp"
#include "openosc/ui/components/meter_reading.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/format/meter.hpp"
#include <cctype>
#include <cstring>
#include "openosc/measurement/dmm/ranges.hpp"
#include <algorithm>
#include <initializer_list>
#include <cstdio>

namespace openosc::ui {
void MeterSummary::create(lv_obj_t *parent) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, lv_pct(100), lv_pct(100));
    lv_obj_set_scrollable(root_, false);
    auto label = [&](const char *text, int x, int y, int width) {
        auto *object = Elements::label(root_, text, x, y);
        lv_obj_set_size(object, width, 16);
        return object;
    };
    reference_ = label("Range unavailable", 0, 0, 150);
    secondary_ = label("", 0, 0, 80);
    range_ = label("", 0, 0, 58);
    separator_ = lv_obj_create(root_);
    lv_obj_remove_style_all(separator_);
    lv_obj_set_style_bg_opa(separator_, LV_OPA_COVER, 0);
    static constexpr const char *names[Columns] = {"MIN", "MAX", "AVG"};
    for (unsigned c = 0; c < Columns; ++c) {
        names_[c] = label(names[c], 0, 0, ColumnWidth);
        values_[c] = label("", 0, 0, ColumnWidth);
    }
    compact_ = false; statisticsRows_ = true; metaY_ = statsY_ = 0;
    bar_.create(root_); // Before arrange(): it places the bar too.
    arrange();
    MeterLook look;
    look.palette = {0, 0xe2e8f0, 0x94a3b8, 0x38bdf8, 0x243449, 0, 0xf87171};
    for (auto &color : look.colors) color = look.palette.ink;
    look.colors[unsigned(MeterRole::StatNames)] = look.palette.muted;
    palette(look);
}
void MeterSummary::compact(bool on, int metaY, int statsY) {
    if (on == compact_ && metaY == metaY_ && statsY == statsY_) return;
    compact_ = on; metaY_ = metaY; statsY_ = statsY;
    arrange();
}
void MeterSummary::statisticsRows(bool rows) {
    if (rows == statisticsRows_) return;
    statisticsRows_ = rows;
    arrange();
}
void MeterSummary::arrange() {
    const auto place = [](lv_obj_t *object, int x, int y, int w, int h) { lv_obj_set_pos(object, x, y); lv_obj_set_size(object, w, h); };
    if (compact_) {
        flowMeta();
        lv_obj_set_style_text_align(range_, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_text_align(secondary_, LV_TEXT_ALIGN_RIGHT, 0);
        for (unsigned c = 0; c < Columns; ++c) centre(c); // Placed again as the values change.
    } else {
        flowMeta();
        lv_obj_set_style_text_align(range_, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_align(secondary_, LV_TEXT_ALIGN_LEFT, 0);
        place(separator_, 0, 24, Width, 1);
        for (unsigned c = 0; c < Columns; ++c) {
            place(names_[c], columnX(c), NameY, ColumnWidth, 16);
            place(values_[c], columnX(c), ValueY, ColumnWidth, ValueHeight);
        }
        bar_.top(statisticsRows_ ? MeterBar::Top : 24); // Without the statistics' rows: right under the meta row.
    }
    lv_obj_set_hidden(separator_, compact_ || !statisticsRows_);
}
// The meta row at the texts' own widths, so no state is clipped by a box sized for another (UNKNOWN
// is wider than MANUAL). Compact: a long REL reference moves the range mode right instead of running
// into it, and the secondary reading (AC frequency) is right-aligned in what is left. Digital: the
// range mode ends at the right edge, the secondary reading takes the room left of it.
void MeterSummary::flowMeta() {
    if (!compact_) {
        const int mode = textWidth(range_), modeX = Width - mode;
        const auto place = [](lv_obj_t *object, int x, int width) { lv_obj_set_pos(object, x, 0); lv_obj_set_size(object, std::max(1, width), MetaHeight); };
        place(reference_, 0, ReferenceWidth);
        place(secondary_, SecondaryX, std::min(SecondaryWidth, modeX - MetaGap - SecondaryX));
        place(range_, modeX, mode);
        return;
    }
    const auto place = [&](lv_obj_t *object, int x, int width) { lv_obj_set_pos(object, x, metaY_); lv_obj_set_size(object, std::max(1, width), CompactRow); };
    const int reference = textWidth(reference_), mode = CompactMetaX + reference + CompactMetaGap;
    place(reference_, CompactMetaX, reference);
    const int secondary = mode + textWidth(range_) + CompactMetaGap;
    place(range_, mode, textWidth(range_));
    place(secondary_, secondary, Width - secondary);
}
namespace {
// "250 Ω", "2.5 kΩ", "60 µF": the reading's unit signs, spaced from the number.
void spaced(double value, Quantity quantity, char *output, size_t capacity) {
    static constexpr const char *units[] = {"", "V", "A", "\xce\xa9", "F", "Hz"};
    char text[32];
    if (!plot::Format::quantity(value, "", text, sizeof text, 6)) { std::snprintf(output, capacity, "----"); return; }
    size_t n = std::strlen(text);
    char prefix[3] = "";
    if (n && std::strchr("pnumkMG", text[n - 1])) {
        if (text[--n] == 'u') std::snprintf(prefix, sizeof prefix, "\xc2\xb5");
        else prefix[0] = text[n];
    }
    std::snprintf(output, capacity, "%.*s %s%s", int(n), text, prefix, units[unsigned(quantity)]);
}
} // namespace
// In the reading's display units (MeterText::unit: kΩ, µF), as the reading's own unit label. A
// resistance or capacitance in the prefix of its own magnitude at the quantum of its readings
// (dmm::DisplayPrefix, Statistics' exponents; GitHub #168), as the main digits are. The number and
// its unit apart, so the caller may leave out a unit the main reading already shows.
void MeterSummary::statisticText(double value, Quantity quantity, Statistic &output, bool relative, int8_t exponent) const {
    static constexpr const char *units[] = {"", "V", "A", "Ohm", "F", "Hz"};
    const auto unit = [&](const char *raw) { std::snprintf(output.unit, sizeof output.unit, "%s", MeterText::unit(raw)); };
    output.number[0] = output.unit[0] = 0;
    if (prefix_.valid() && prefix_.quantity() == quantity) {
        const auto prefix = exponent != dmm::DisplayPrefix::Unknown ? prefix_.at(exponent) : prefix_;
        const char *prefixed = nullptr;
        if (prefix.format(value, output.number, sizeof output.number, prefixed)) { unit(prefixed); return; }
    }
    if (resolution_.valid() && resolution_.quantity() == quantity &&
        resolution_.format(value, "", output.number, sizeof output.number, relative)) { unit(resolution_.unit()); return; }
    if (quantity == Quantity::Temperature) { std::snprintf(output.number, sizeof output.number, "----"); return; }
    char text[24];
    const char *base = units[unsigned(quantity)];
    if (!plot::Format::quantity(value, base, text, sizeof text, dmm::Resolution::MaxDigits)) {
        std::snprintf(output.number, sizeof output.number, "----");
        return;
    }
    // The number and its SI prefix, then the prefixed unit in its display form.
    const size_t length = std::strlen(text), plain = std::strlen(base);
    const size_t prefixed = length >= plain + 1 && std::isalpha(uint8_t(text[length - plain - 1])) ? plain + 1 : plain;
    const size_t cut = length - std::min(prefixed, length);
    unit(text + cut);
    text[cut] = 0;
    std::snprintf(output.number, sizeof output.number, "%s", text);
}
int MeterSummary::textWidth(lv_obj_t *label) {
    lv_point_t size;
    lv_text_get_size(&size, lv_label_get_text(label), lv_obj_get_style_text_font(label, LV_PART_MAIN), 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    return size.x;
}
// A compact column: the name and the value side by side, the pair centred in the column's third.
void MeterSummary::centre(unsigned c) {
    const int name = textWidth(names_[c]);
    const int value = MeterReading::contentWidth(values_[c]);
    const int x = columnX(c) + std::max(0, (ColumnWidth - name - CompactNameGap - value) / 2);
    lv_obj_set_pos(names_[c], x, statsY_);
    lv_obj_set_size(names_[c], name, CompactRow);
    lv_obj_set_pos(values_[c], x + name + CompactNameGap, statsY_);
    lv_obj_set_size(values_[c], std::max(1, std::min(value, ColumnWidth - name - CompactNameGap)), CompactRow);
}
namespace {
constexpr MeterRole valueRoles[MeterSummary::Columns] = {MeterRole::Minimum, MeterRole::Maximum, MeterRole::Average};
void font(lv_obj_t *label, const lv_font_t *font) {
    if (lv_obj_get_style_text_font(label, LV_PART_MAIN) != font) lv_obj_set_style_text_font(label, font, 0);
}
} // namespace
void MeterSummary::palette(const MeterLook &look) {
    look_ = look;
    for (unsigned c = 0; c < Columns; ++c) {
        Elements::textColor(names_[c], look.color(MeterRole::StatNames));
        font(names_[c], look.text(MeterRole::StatNames, CompactFont));
        Elements::textColor(values_[c], look.color(valueRoles[c]));
    }
    Elements::textColor(reference_, look.color(MeterRole::Range));
    Elements::textColor(secondary_, look.color(MeterRole::Secondary));
    font(reference_, look.text(MeterRole::Range, CompactFont));
    font(secondary_, look.text(MeterRole::Secondary, CompactFont));
    font(range_, look.text(MeterRole::Mode, CompactFont));
    flowMeta(); // The texts' widths follow their fonts.
    if (compact_) for (unsigned c = 0; c < Columns; ++c) centre(c);
    const auto line = lv_color_hex(look.palette.line);
    if (!lv_color_eq(lv_obj_get_style_bg_color(separator_, LV_PART_MAIN), line)) lv_obj_set_style_bg_color(separator_, line, 0);
    bar_.palette(look.palette);
}
void MeterSummary::visible(bool value) { lv_obj_set_hidden(root_, !value); }
void MeterSummary::update(const dmm::Result &result, const dmm::Statistics *statistics, uint32_t accent,
                          double reference, bool relative, bool peerRelative, const char *charge, const char *primaryUnit) {
    const double offset = relative ? reference : 0;
    if (const auto resolution = dmm::Resolution::of(result); resolution.valid()) {
        resolution_ = resolution;
        prefix_ = dmm::DisplayPrefix::of(result);
    }
    const bool temperature = result.primary.quantity == Quantity::Temperature;
    bool changed = Elements::setText(secondary_, result.secondary.valid() ? result.secondaryText : charge ? charge : "");
    if (relative) {
        char value[sizeof(Statistic::number) + sizeof(Statistic::unit)], text[48];
        if (temperature) {
            Statistic shown;
            statisticText(reference, result.primary.quantity, shown);
            std::snprintf(value, sizeof value, "%s%s", shown.number, shown.unit);
        } else spaced(reference, result.primary.quantity, value, sizeof value);
        std::snprintf(text, sizeof text, "REL ref %s", value); changed |= Elements::setText(reference_, text);
    } else if (peerRelative) changed |= Elements::setText(reference_, "REL ref: controller");
    else if (temperature) changed |= Elements::setText(reference_, "Temperature");
    else if (const auto *range = dmm::Ranges::current(result)) {
        char value[32], text[48];
        spaced(range->maximumSi, result.primary.quantity, value, sizeof value);
        std::snprintf(text, sizeof text, "Range %s", value); changed |= Elements::setText(reference_, text);
    } else changed |= Elements::setText(reference_, "Range unavailable");
    changed |= Elements::setText(range_, temperature ? "" : result.range == dmm::RangeMode::Auto ? "AUTO"
                                         : result.range == dmm::RangeMode::Manual ? "MANUAL" : "UNKNOWN");
    if (changed) flowMeta();
    Elements::textColor(range_, accent);
    // Statistics shown (On, not Trend): the names stay, and an empty period (after a reset, an
    // overload or no reading, or of another quantity) shows "--" as the factory's placeholders.
    const bool shown = statistics != nullptr;
    const auto view = shown ? dmm::StatisticsView::of(*statistics, result.primary.quantity, offset) : dmm::StatisticsView{};
    char texts[Columns][sizeof(Statistic::number) + sizeof(Statistic::unit)] = {};
    if (shown && !view.valid)
        for (auto &text : texts) std::snprintf(text, sizeof text, "--");
    if (view.valid) {
        const double numbers[Columns] = {view.minimum, view.maximum, view.average};
        const int8_t exponents[Columns] = {view.minimumExponent, view.maximumExponent, view.averageExponent};
        Statistic values[Columns];
        for (unsigned c = 0; c < Columns; ++c)
            statisticText(numbers[c], view.quantity, values[c], relative || peerRelative, exponents[c]);
        // The unit once, beside the main digits, when all three are in it (GitHub #185); else each its own.
        bool shared = primaryUnit && *primaryUnit;
        for (const auto &value : values) shared = shared && !std::strcmp(value.unit, primaryUnit);
        for (unsigned c = 0; c < Columns; ++c)
            std::snprintf(texts[c], sizeof texts[c], "%.23s%.11s", values[c].number, shared ? "" : values[c].unit);
    }
    // One size for the three, each in its own typeface: the largest (ValueFont, then 12; compact:
    // 12 beside the names) in which all three fit their columns, so they share a baseline.
    unsigned size = compact_ ? CompactFont : ValueFont;
    const auto fits = [&](unsigned c, unsigned candidate) {
        const int room = compact_ ? ColumnWidth - textWidth(names_[c]) - CompactNameGap : ColumnWidth;
        return MeterReading::fits(texts[c], room, look_.text(valueRoles[c], candidate));
    };
    for (; size > CompactFont; size = CompactFont) {
        bool all = true;
        for (unsigned c = 0; c < Columns; ++c) all = all && fits(c, size);
        if (all) break;
    }
    for (unsigned c = 0; c < Columns; ++c) {
        MeterReading::show(values_[c], texts[c], look_.text(valueRoles[c], size));
        if (compact_) centre(c);
        // Statistics Off or Trend: neither names nor values.
        lv_obj_set_hidden(names_[c], !shown);
        lv_obj_set_hidden(values_[c], !shown);
    }
}
} // namespace openosc::ui
