#include "openosc/ui/screens/meter_screen.hpp"
#include <algorithm>
#include <cstdio>
#include "openosc/ui/format/meter.hpp"
#include "openosc/ui/components/meter_reading.hpp"
#include "openosc/ui/components/meter_style.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/measurement/dmm/terminals.hpp"
#include "openosc/measurement/dmm/resolution.hpp"
#include <cstring>
#include <initializer_list>

namespace openosc::ui {
namespace {
constexpr unsigned Mode = unsigned(MeasurementMode::Multimeter);
}
void MeterScreen::activate(const ApplicationState &) {
    temperatureAxis_.reset();
    cards(dmm::Function::Unknown, false, false, ParameterBar::Count, "");
    host_.status("NO DATA", StatusTone::Warning);
    styled_ = false; // The theme was applied again: so are the DMM colours.
    style(face_, palette_, appearance_);
    layout(analog_);
    host_.functionIcon().show(dmm::Function::Unknown, false, look_.color(MeterRole::Function));
    reading("----.--");
    host_.elements().text(ElementId::Unit, "");
    host_.elements().text(ElementId::Details, "Waiting");
    host_.elements().text(ElementId::Measurements, "");
    host_.meterSummary().update({}, nullptr, look_.color(MeterRole::Mode));
    feed({}, DataStatus::NoData, nullptr);
}
// The DMM body's look over the theme's: the panel, the element texts and fonts, the components and
// the cards' selection (ScreenHost sets the theme's again on every mode change).
void MeterScreen::style(unsigned face, unsigned palette, const MeterAppearance &appearance) {
    if (styled_ && face == face_ && palette == palette_ && appearance == appearance_) return;
    face_ = face; palette_ = palette; appearance_ = appearance; styled_ = true;
    const auto &theme = host_.theme();
    auto parameters = theme.elements[Mode][unsigned(ElementId::Parameters)];
    look_ = MeterStyles::look(palette, face, appearance, theme, parameters.color);
    auto &elements = host_.elements();
    auto *panel = host_.meterPanel();
    lv_obj_set_style_bg_color(panel, lv_color_hex(look_.color(MeterRole::Background)), 0);
    lv_obj_set_pos(panel, 0, ScreenLayout::Top); // The whole body: the soft keys draw their own cards over it.
    lv_obj_set_size(panel, 320, ScreenLayout::Body);
    lv_obj_set_hidden(panel, false);
    Elements::textColor(elements.get(ElementId::Reading), look_.color(MeterRole::Reading));
    auto *unit = elements.get(ElementId::Unit);
    Elements::textColor(unit, look_.color(MeterRole::Unit));
    // The unit in its role's typeface at the theme's size (24: IBM Plex Sans or Noto Sans Mono 24).
    const auto *unitFont = look_.text(MeterRole::Unit, theme.elements[Mode][unsigned(ElementId::Unit)].font);
    if (lv_obj_get_style_text_font(unit, LV_PART_MAIN) != unitFont) lv_obj_set_style_text_font(unit, unitFont, 0);
    Elements::textColor(elements.get(ElementId::Details), look_.color(MeterRole::Details));
    Elements::textColor(elements.get(ElementId::Measurements), look_.palette.ink);
    host_.meterSummary().palette(look_);
    host_.meterTrend().palette(look_);
    host_.dial().palette(look_);
    parameters.color = look_.color(MeterRole::CardSelection);
    host_.parameters().palette(theme, parameters);
    // An explicit Card text colours the selected card's text too (Card frame: only its frame);
    // Default keeps the theme's behaviour: the selected text in the frame's colour.
    host_.parameters().text(look_.text(MeterRole::CardText, parameters.font), look_.color(MeterRole::CardText),
                            appearance[MeterRole::CardText].color != MeterRoleStyle::Inherit);
    // The info line's font at the layout's size; nothing is shown or hidden here (a menu may be open).
    const unsigned details = analog_ ? MeterSummary::CompactFont : theme.elements[Mode][unsigned(ElementId::Details)].font;
    auto *line = elements.get(ElementId::Details);
    if (lv_obj_get_style_text_font(line, LV_PART_MAIN) != look_.text(MeterRole::Details, details))
        lv_obj_set_style_text_font(line, look_.text(MeterRole::Details, details), 0);
}
// Digital (ScreenLayout::digital): the reading block is the sign row (function sign, jacks, unit badge)
// over the 64 px digits, the measurements block the summary (meta row, MIN/MAX/AVG, bar) or the
// trend. Analog (ScreenLayout::analog): the dial block is the sign and the meta row (range, range
// mode, AC frequency) over the 92 px dial with RMS and the jacks as its marking; the measurements
// block MIN/MAX/AVG on one row; the reading block the 64 px digits with their unit centred. The soft
// keys are the order's Buttons block. The theme's x, widths and fonts stay; its y inside a block
// are taken relative to its default body top.
void MeterScreen::layout(bool analog) {
    analog_ = analog;
    const auto *t = host_.theme().elements[Mode];
    const auto &reading = t[unsigned(ElementId::Reading)], &unit = t[unsigned(ElementId::Unit)];
    const auto &details = t[unsigned(ElementId::Details)], &rows = t[unsigned(ElementId::Measurements)];
    auto &elements = host_.elements();
    auto *summary = elements.get(ElementId::Measurements), *line = elements.get(ElementId::Details);
    auto *value = elements.get(ElementId::Reading), *unitLabel = elements.get(ElementId::Unit);
    const auto place = [](lv_obj_t *object, int x, int y, int w, int h) { lv_obj_set_pos(object, x, y); lv_obj_set_size(object, w, h); };
    host_.functionIcon().visible(true);
    host_.meterSummary().bar(!analog && bar_);
    MeterReading::release(line);
    Elements::textColor(line, look_.color(MeterRole::Details));
    if (analog) {
        ScreenLayout::analog(analogOrder_, statistics_, analogSpans_);
        const auto &dial = analogSpans_[unsigned(AnalogBlock::Dial)], &stats = analogSpans_[unsigned(AnalogBlock::Measurements)];
        // The summary spans the body: its meta row goes with the dial, its statistics row stands alone.
        place(summary, rows.x, ScreenLayout::Top, rows.width, ScreenLayout::Body);
        host_.meterSummary().compact(true, dial.y - ScreenLayout::Top, stats.shown ? stats.y - ScreenLayout::Top : -1);
        host_.meterSummary().statisticsRows(true);
        host_.functionIcon().place(reading.x, dial.y + 1);
        host_.dial().place(reading.x, dial.y + ScreenLayout::DialOffset, reading.width, ScreenLayout::DialHeight);
        // The info line's right slot in the meta row (only "Waiting" before a packet; the jacks are the dial's marking).
        const int jacks = rows.x + MeterSummary::CompactMetaX + 158;
        place(line, jacks, dial.y, rows.x + rows.width - jacks, MeterSummary::CompactRow);
        lv_obj_set_style_text_font(line, look_.text(MeterRole::Details, MeterSummary::CompactFont), 0);
        lv_obj_set_style_text_align(line, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_align(unitLabel, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_hidden(value, false);
        host_.buttons(analogSpans_[unsigned(AnalogBlock::Buttons)].y);
    } else {
        ScreenLayout::digital(digital_, statistics_, bar_, digitalSpans_);
        const auto &body = digitalSpans_[unsigned(DigitalBlock::Reading)], &meter = digitalSpans_[unsigned(DigitalBlock::Measurements)];
        digits_ = ScreenLayout::digitsFor(body.height);
        const int grown = digits_ - ScreenLayout::Digits;
        const int dy = body.y + (body.height - ScreenLayout::DigitalReading - grown) / 2 - ThemeBodyTop; // Centred in its block.
        host_.functionIcon().place(reading.x, unit.y + dy);
        place(summary, rows.x, meter.y, rows.width, meter.height);
        host_.meterSummary().compact(false);
        host_.meterSummary().statisticsRows(statistics_);
        place(line, details.x, details.y + dy, details.width, details.height);
        lv_obj_set_style_text_font(line, look_.text(MeterRole::Details, details.font), 0);
        lv_obj_set_style_text_align(line, LV_TEXT_ALIGN_LEFT, 0);
        place(value, reading.x, reading.y + dy, reading.width, reading.height + grown);
        place(unitLabel, unit.x, unit.y + dy, unit.width, unit.height);
        lv_obj_set_style_text_align(unitLabel, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_hidden(value, !reading.visible);
        host_.buttons(digitalSpans_[unsigned(DigitalBlock::Buttons)].y);
    }
    host_.dial().visible(analog);
}
void MeterScreen::arrangement(const DigitalArrangement &digital, const AnalogArrangement &analog, bool shown) {
    if (!digital.valid() || !analog.valid() || (digital == digital_ && analog == analogOrder_)) return;
    digital_ = digital; analogOrder_ = analog;
    if (shown) layout(analog_);
}
void MeterScreen::cards(dmm::Function function, bool beeper, bool currentInput, unsigned focus, const char *unit) {
    // The zones stand over F1..F4: their signs say the functions, the selected one its unit; no key names.
    auto &bar = host_.parameters();
    for (unsigned i = 0; i < ParameterBar::Count; ++i) bar.item(i, "", i == focus ? unit : "");
    bar.select(focus);
    host_.meterCards().show(function, beeper, currentInput, look_.color(MeterRole::CardActive), look_.color(MeterRole::CardInactive));
    host_.meterCards().visible(true);
}
void MeterScreen::reading(const char *text) {
    const auto &theme = host_.theme().elements[Mode];
    const auto &box = theme[unsigned(ElementId::Reading)];
    auto &elements = host_.elements();
    auto *label = elements.get(ElementId::Reading);
    // The theme's default Digits takes the block's size (digitsFor); a larger Reading font is kept up to it.
    const int font = analog_ || box.font < ScreenLayout::Digits ? box.font : box.font == ScreenLayout::Digits ? digits_ : std::min<int>(box.font, digits_);
    const auto fonts = MeterStyles::reading(look_.face, decltype(box.font)(font));
    const bool changed = std::strcmp(lv_label_get_text(label), text) != 0;
    if (!analog_) MeterReading::show(label, text, fonts);
    else {
        const auto &span = analogSpans_[unsigned(AnalogBlock::Reading)];
        const lv_area_t area = {box.x, span.y, box.x + box.width - 1, span.y + span.height - 1};
        MeterReading::showCentred(label, elements.get(ElementId::Unit), text, fonts, area, UnitGap);
    }
    // The 64 px digits redraw beyond their box (LVGL's extra draw margin of the face). Where that
    // reaches the soft keys next to the reading block, the keys are redrawn whole with it: a card
    // cut by the margin's edge would not repaint exactly as a whole redraw (its sign lines).
    if (!changed) return;
    static constexpr int DrawMargin = 20; // Over the faces' extra draw size at 80 px.
    lv_obj_update_layout(label);
    lv_area_t reach, keys;
    lv_obj_get_coords(label, &reach);
    auto *bar = elements.get(ElementId::Parameters);
    lv_obj_get_coords(bar, &keys);
    const bool reaches = reach.y2 + DrawMargin >= keys.y1 && reach.y1 - DrawMargin <= keys.y2 &&
                         reach.x2 + DrawMargin >= keys.x1 && reach.x1 - DrawMargin <= keys.x2;
    if (!lv_obj_is_hidden(bar) && reaches) lv_obj_invalidate(bar);
}
void MeterScreen::feed(const dmm::Result &result, DataStatus status, const dmm::RelativeReading *relative) {
    if (analog_) {
        const auto bar = dmm::BarReading::from(result, status, relative, &temperatureAxis_);
        auto &dial = host_.dial();
        const auto decimals = bar.decimals(result);
        const bool scaleChanged = decimals != 255 && dial.scale(decimals, MeterText::unit(bar.unit(result)), bar.scale);
        dial.reading(bar, held_, bar.temperature && scaleChanged);
    }
    else if (bar_) host_.meterSummary().bar(result, status, look_.color(MeterRole::Bar), relative, &temperatureAxis_);
}
void MeterScreen::fastBar(const dmm::Result &result, DataStatus status, const dmm::RelativeReading *relative, uint32_t now) {
    if (host_.overlayActive()) return; // Under a menu: neither the bar nor the needle (hidden) is fed.
    feed(result, status, relative);
    if (analog_) host_.dial().service(now);
}
void MeterScreen::status(const dmm::Result &result, bool held, DataStatus status, bool relative, const char *mode) {
    const bool armed = mode && *mode; // A hold mode's cycle: its word, never "LIVE" for a held reading.
    const char *word = armed ? mode : "HOLD";
    // The link's state (NO DATA, STALE) first, else the shown reading's own (MeterText::caption):
    // the DMM's "no number yet" (NoReading) is neutral, only an overload or an unreadable reading alerts.
    const bool link = status == DataStatus::Stale || status == DataStatus::NoData;
    const auto reading = MeterText::state(result);
    const char *state = held ? (status == DataStatus::Stale ? "STALE" : "")
                        : link ? MeterText::caption(status)
                        : armed ? ""
                        : reading == DataStatus::Overload ? "OVERLOAD"
                        : reading == DataStatus::Ok ? "LIVE" : MeterText::caption(reading);
    const bool alert = reading == DataStatus::Overload || (!link && MeterText::alert(reading));
    const auto tone = held || (!armed && alert) ? StatusTone::Alert
                      : link ? StatusTone::Warning
                      : armed || reading == DataStatus::NoReading ? StatusTone::Neutral // Waiting over live readings.
                      : StatusTone::Good;
    const bool prefix = held || armed;
    char text[48];
    std::snprintf(text, sizeof text, "%s%s%s%s%s", prefix ? word : "", prefix && *state ? " / " : "", state,
                  relative || result.peerRelative ? " REL" : "", recording_ ? " REC" : ""); // "HOLD / STALE REL", "LIVE REC".
    host_.status(text, tone);
}
void MeterScreen::update(const dmm::Result &result, bool held, DataStatus status,
                         const MeterContext &context) {
    // A menu over the DMM: only the state pill follows the readings. The body (its look, places,
    // reading, cards) waits for the DMM to show again, so nothing hidden comes back over the menu
    // and the menu's soft keys stay where it put them; the session and its statistics run on.
    recording_ = context.recording;
    if (host_.overlayActive()) {
        this->status(result, held, status, context.relative && context.relative->matches(result), context.hold);
        return;
    }
    auto &elements = host_.elements();
    held_ = held;
    style(context.face, context.palette, context.appearance ? *context.appearance : MeterAppearance());
    // The measurements block's rows: MIN/MAX/AVG when shown (Statistics On, the statistics page) or
    // the trend; without them it shrinks (digital) or is absent (analog) and the rest closes up.
    const bool statisticsRows = context.display == 2 || context.showStatistics || context.display == 1;
    if ((context.display == AnalogDisplay) != analog_ || statisticsRows != statistics_ || context.showBar != bar_) {
        statistics_ = statisticsRows;
        bar_ = context.showBar;
        layout(context.display == AnalogDisplay);
    }
    const bool relative = context.relative && context.relative->matches(result);
    this->status(result, held, status, relative, context.hold);
    host_.meterSummary().barUnit(context.showBarUnit); // Also over a held or no reading.
    feed(result, status, context.relative);
    const double reference = relative ? context.relative->reference().si : 0;
    const auto *statistics = context.statistics;
    char text[128];
    if (!MeterText::displayReading(result, text, sizeof text)) std::snprintf(text, sizeof text, "----.--");
    elements.text(ElementId::Unit, MeterText::unit(result.unit));
    const bool mean = context.display == 1 && statistics && statistics->count() &&
                      statistics->quantity() == result.primary.quantity;
    if ((relative || mean) && result.primary.valid()) {
        NumericValue unit;
        const double factor = UnitParser::parse("1", result.unit, unit) ? unit.si : 1;
        std::snprintf(text, sizeof text, "--");
        const unsigned decimals = dmm::Resolution::decimals(result);
        const double measured = (mean ? statistics->average() : result.primary.si) - reference;
        const double value = result.primary.quantity == Quantity::Temperature
            ? dmm::TypeK::display(measured, result.unit, relative || result.peerRelative) : measured / factor;
        if (!Decimal::fixed(value, decimals, text, sizeof text)) Decimal::number(value, text, sizeof text, 6);
    }
    const auto function = context.selecting != dmm::Function::Unknown ? context.selecting : dmm::Functions::identify(result);
    const char *terminals = dmm::Terminals::forFunction(function);
    // The function as its sign; the row says only what the sign cannot: MEAN, RMS, the jacks.
    host_.functionIcon().show(function, result.kind == dmm::Kind::DiodeWithBeeper, look_.color(MeterRole::Function));
    reading(text);
    // AC V.F.C. (the ZT706S's third F1 position) is V AC with its mark, as on the factory screen:
    // "VFC" first on the sign row (digital) or on the dial's marking (analog: the meta row's right
    // slot holds the AC frequency), and on the F1 card's value line.
    const bool vfc = dmm::Functions::vfc(result);
    char details[64];
    if (!analog_) std::snprintf(details, sizeof details, "%s%s%s%s", vfc ? "VFC  " : "", mean ? "MEAN  " : "", result.trueRms ? "RMS  " : "", terminals);
    else { // The meta row is the range's, its mode's and the AC frequency's; RMS and the jacks are the dial's marking.
        char marking[32];
        std::snprintf(marking, sizeof marking, "%s%s%s", vfc ? "VFC " : "", result.trueRms ? "RMS " : "", terminals);
        host_.dial().marking(marking);
        details[0] = 0;
    }
    elements.text(ElementId::Details, details);
    // The card of the recognised function's key group (the diode is F2 Ohm / C although it reads
    // volts); an unrecognised function selects none.
    static constexpr unsigned groups[] = {0, 1, 2, 3, 3}; // Voltage, ResistanceGroup, Current, Millivolts, Milliamps.
    const auto group = dmm::Functions::group(function);
    const unsigned focus = function == dmm::Function::Unknown || unsigned(group) >= sizeof groups / sizeof *groups
                               ? ParameterBar::Count : groups[unsigned(group)];
    char value[24];
    std::snprintf(value, sizeof value, "%s%s", MeterText::unit(result.unit), vfc ? " VFC" : "");
    cards(function, result.kind == dmm::Kind::DiodeWithBeeper, result.currentInput, focus, value);
    elements.text(ElementId::Measurements, "");
    host_.meterSummary().visible(true);
    host_.meterTrend().visible(context.display == 2);
    if (context.display == 2) host_.meterTrend().update(context.history, result.primary.quantity, reference, relative, result.peerRelative, result.unit);
    // DC current: the charge since the function was chosen, in the meta row's secondary slot (which
    // holds the AC frequency only), so it has its place in every layout, order and look.
    char charge[24] = "";
    if (context.charge && dmm::ChargeCounter::counts(function)) dmm::ChargeCounter::format(context.charge->ampereHours(), charge, sizeof charge);
    host_.meterSummary().update(result, context.display != 2 && (context.showStatistics || context.display == 1) ? statistics : nullptr,
                                look_.color(MeterRole::Mode), reference, relative, result.peerRelative, charge);
}
} // namespace openosc::ui
