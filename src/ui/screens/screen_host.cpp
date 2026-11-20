#include "openosc/ui/screens/screen_host.hpp"
#include "openosc/ui/components/meter_reading.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::ui {
void ScreenHost::status(const char *text, StatusTone tone) {
    elements_.text(ElementId::Status, text);
    lv_obj_set_width(elements_.get(ElementId::Status), LV_SIZE_CONTENT); // The theme re-applies its box on every activation.
    statusWidth_ = std::min(Elements::textWidth(text) + 2 * StatusBarLayout::PillPadding, StatusBarLayout::MaximumStatus);
    statusRight_ = statusX_ + statusWidth_;
    if (tone == tone_) return;
    tone_ = tone;
    static constexpr struct { uint32_t background, text; } colors[] = {
        {0x334155, 0xe2e8f0}, {0x22c55e, 0x052e16}, {0xfbbf24, 0x1c1400}, {0xef4444, 0xffffff}};
    auto *pill = elements_.get(ElementId::Status);
    lv_obj_set_style_bg_color(pill, lv_color_hex(colors[unsigned(tone)].background), 0);
    lv_obj_set_style_text_color(pill, lv_color_hex(colors[unsigned(tone)].text), 0);
}
void ScreenHost::statusX(int x) {
    if (x == statusX_) return;
    statusX_ = x; statusRight_ = statusX_ + statusWidth_;
    if (root_) placeStatus();
}
void ScreenHost::placeStatus() { lv_obj_set_pos(elements_.get(ElementId::Status), statusX_, 1); }
void ScreenHost::build(lv_obj_t *parent, const Theme &theme) {
    clear();
    theme_ = &theme;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 320, 240);
    lv_obj_set_scrollable(root_, false);
    lv_obj_add_event_cb(root_, [](lv_event_t *event) {
        static_cast<ScreenHost *>(lv_event_get_user_data(event))->root_ = nullptr;
    }, LV_EVENT_DELETE, this);
    meterPanel_ = lv_obj_create(root_); // First: below everything the theme places.
    lv_obj_remove_style_all(meterPanel_);
    lv_obj_set_scrollable(meterPanel_, false);
    lv_obj_set_style_bg_opa(meterPanel_, LV_OPA_COVER, 0);
    for (unsigned i = 0; i < ElementCount; ++i) {
        const auto id = static_cast<ElementId>(i);
        lv_obj_t *object;
        switch (id) {
        case ElementId::Waveform: object = waveform_.create(root_); break;
        case ElementId::Parameters: object = parameters_.create(root_); break;
        case ElementId::Preview: object = preview_.create(root_); break;
        case ElementId::Icon: object = lv_image_create(root_); break;
        default: object = Elements::label(root_, ""); break;
        }
        elements_.bind(id, object);
    }
    chrome_ = lv_obj_create(root_);
    lv_obj_remove_style_all(chrome_);
    lv_obj_set_size(chrome_, StatusBarLayout::Width, StatusBarLayout::Height);
    lv_obj_set_scrollable(chrome_, false);
    lv_obj_set_style_bg_opa(chrome_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(chrome_, lv_color_hex(theme.background), 0);
    auto *pill = elements_.get(ElementId::Status);
    lv_obj_set_parent(pill, chrome_);
    lv_obj_set_style_pad_hor(pill, StatusBarLayout::PillPadding, 0);
    lv_obj_set_style_max_width(pill, StatusBarLayout::MaximumStatus, 0); // Clipped before the FPS column.
    lv_obj_set_style_pad_top(pill, 2, 0); // Centres the capitals on the bar's axis, y=10.
    lv_obj_set_style_radius(pill, StatusBarLayout::PillRadius, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    tone_ = StatusTone(255);
    meterSummary_.create(elements_.get(ElementId::Measurements));
    scopeFooter_.create(elements_.get(ElementId::Measurements), theme);
    meterTrend_.create(elements_.get(ElementId::Measurements), theme);
    dial_.create(root_);         // The dial first: the function sign is drawn over its empty corner.
    functionIcon_.create(root_);
    meterCards_.create(parameters_);
    decodeTable_.create(root_); // Two objects for the decoder, hidden until it shows.
    decodeStrip_.create(root_);
    waveform_.inlineScales(theme.layout == LayoutPreset::Legacy);
    waveform_.colors(theme);
    waveform_.configure(theme.plot);
    waveform_.clear();
}
void ScreenHost::activate(MeasurementMode mode) {
    mode_ = mode; // First: theme() follows the mode.
    tone_ = StatusTone(255); // Applying the theme below resets the pill colours.
    overlay_ = false;
    meterTrend_.visible(false);
    meterSummary_.visible(mode == MeasurementMode::Multimeter); // Every preset has the same DMM body.
    scopeFooter_.visible(mode == MeasurementMode::Oscilloscope); // Every preset, Legacy too: six readouts.
    // The DMM shows these itself (MeterScreen); every other screen has none.
    lv_obj_set_hidden(meterPanel_, true);
    functionIcon_.visible(false);
    dial_.visible(false);
    meterCards_.visible(false);
    decodeTable_.hide(); decodeStrip_.hide(); // The Scope places them again (placeScope).
    if (mode != MeasurementMode::Oscilloscope) parameters_.untinted(); // Channel colours are the Scope's cards' alone.
    const auto &theme = this->theme();
    // The body behind the blocks (the small gaps between them) in this screen's background.
    lv_obj_set_style_bg_color(root_, lv_color_hex(theme.background), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    elements_.apply(theme.elements[unsigned(mode)]);
    MeterReading::release(elements_.get(ElementId::Reading)); // The DMM sets its sign column again on activation.
    // Status is part of the instrument shell, never a hideable body element.
    auto *status = elements_.get(ElementId::Status);
    lv_obj_set_size(status, StatusBarLayout::MaximumStatus, StatusBarLayout::PillHeight);
    placeStatus();
    lv_obj_set_style_text_font(status, Elements::font(12), 0);
    lv_obj_set_hidden(status, false);
    lv_obj_move_foreground(chrome_);
    // One soft-key bar on every screen: the theme's y (the DMM and the Scope place it by their order).
    buttons(theme.elements[unsigned(mode)][unsigned(ElementId::Parameters)].y);
    paintScope();
    if (mode == MeasurementMode::Oscilloscope) placeScope();
    if (mode == MeasurementMode::Generator) placeGenerator();
    parameters_.palette(theme, theme.elements[unsigned(mode)][unsigned(ElementId::Parameters)]);
    lv_obj_set_hidden(root_, false);
}
void ScreenHost::scopeTheme(const Theme *theme, const ScopeLook *look, bool apply) {
    scopeTheme_ = theme; // Its contents may have changed at the same address: applied again.
    scopeLook_ = look;
    if (!apply) return;
    // Shown: the shell takes the colours at once, as on activation (the elements' colours and fonts,
    // the order's places, the waveform, footer and soft keys); the record and the texts stay.
    if (!root_ || mode_ != MeasurementMode::Oscilloscope || overlay_) return;
    const auto tone = tone_;
    char text[48];
    std::snprintf(text, sizeof text, "%s", lv_label_get_text(elements_.get(ElementId::Status)));
    activate(mode_);
    if (tone != StatusTone(255)) status(text, tone); // The pill as it was (activate resets its box and colours).
}
void ScreenHost::paintScope() {
    const auto &t = theme();
    lv_obj_set_style_bg_color(chrome_, lv_color_hex(t.background), 0);
    waveform_.colors(t);
    if (scopeLook_ && mode_ == MeasurementMode::Oscilloscope)
        waveform_.roles(scopeLook_->color(ScopeRole::Trigger), scopeLook_->color(ScopeRole::TagText), scopeLook_->color(ScopeRole::Readout));
    else waveform_.roles(plot::Palette::FollowSource, plot::Palette::Contrast, t.foreground);
    scopeFooter_.theme(t);
    decodeTable_.colors(t); decodeStrip_.colors(t);
}
void ScreenHost::buttons(int y) {
    auto *bar = elements_.get(ElementId::Parameters);
    lv_obj_set_pos(bar, ScreenLayout::ButtonsX, y);
    lv_obj_set_size(bar, ScreenLayout::ButtonsWidth, ScreenLayout::ButtonsHeight);
    parameters_.edge(y + ScreenLayout::ButtonsHeight >= ScreenLayout::Bottom); // On the screen's bottom edge: square bottom corners.
}
void ScreenHost::scopeArrangement(const ScopeArrangement &order) {
    if (!order.valid() || order == scopeOrder_) return;
    scopeOrder_ = order;
    if (root_ && mode_ == MeasurementMode::Oscilloscope && !overlay_) placeScope();
}
// The order's spans for the three Scope blocks. Every preset takes them: the soft keys, the six
// readouts and the 300x156 plot are shown in Compact and Legacy too (their old top line, one-line
// readouts and narrower plot gave way; Legacy keeps its inline plot scales). The theme's colours,
// fonts and the waveform's x and width stay.
void ScreenHost::placeScope() {
    ScreenLayout::scope(scopeOrder_, true, scopeSpans_);
    const auto *t = theme().elements[unsigned(MeasurementMode::Oscilloscope)];
    const auto &readouts = scopeSpans_[unsigned(ScopeBlock::Measurements)], &plot = scopeSpans_[unsigned(ScopeBlock::Waveform)];
    const auto &keys = scopeSpans_[unsigned(ScopeBlock::Buttons)];
    auto *measurements = elements_.get(ElementId::Measurements), *waveform = elements_.get(ElementId::Waveform);
    const auto &w = t[unsigned(ElementId::Waveform)];
    lv_obj_set_hidden(elements_.get(ElementId::Details), true); // Compact's scale line: the Scale and Position zones say it.
    lv_obj_set_hidden(elements_.get(ElementId::Parameters), false);
    if (decoderShown_ && decoderView_ != decoding::View::Annotations) { // The decoder's own layout (§4.2).
        const bool split = decoderView_ == decoding::View::Split;
        lv_obj_set_hidden(measurements, true);
        if (split) {
            lv_obj_set_pos(waveform, w.x, ScreenLayout::Top);
            lv_obj_set_size(waveform, w.width, ScreenLayout::SplitWaveform);
        }
        lv_obj_set_hidden(waveform, !split);
        decodeTable_.place(split ? ScreenLayout::Top + ScreenLayout::SplitWaveform : ScreenLayout::Top,
                           split ? ScreenLayout::SplitTable : ScreenLayout::DecoderOnlyTable);
        decodeStrip_.hide();
        buttons(ScreenLayout::MenuKeysY);
        return;
    }
    decodeTable_.hide();
    // The readouts row is the footer's: the full 320 px of its two panels (a theme's narrower box,
    // Legacy's x 10 / 300, would push the second channel's panel off the screen).
    lv_obj_set_pos(measurements, 0, readouts.y);
    lv_obj_set_size(measurements, StatusBarLayout::Width, readouts.height);
    lv_obj_set_hidden(measurements, false);
    lv_obj_set_pos(waveform, w.x, plot.y);
    lv_obj_set_size(waveform, w.width, plot.height);
    lv_obj_set_hidden(waveform, false);
    buttons(keys.y);
    // Wave + annotations: the strip over the plot's bottom rows, in the plot's columns.
    if (decoderShown_) decodeStrip_.place(w.x + plot::PlotFrame::Left, plot.bottom() + 1 - ScreenLayout::AnnotationStrip,
                                          plot::PlotFrame::plotWidth(w.width), ScreenLayout::AnnotationStrip);
    else decodeStrip_.hide();
}
void ScreenHost::decoder(bool shown, decoding::View view) {
    if (shown == decoderShown_ && view == decoderView_) return;
    decoderShown_ = shown; decoderView_ = view;
    if (root_ && mode_ == MeasurementMode::Oscilloscope && !overlay_) placeScope();
}
void ScreenHost::generatorArrangement(const GeneratorArrangement &order) {
    if (!order.valid() || order == generatorOrder_) return;
    generatorOrder_ = order;
    if (root_ && mode_ == MeasurementMode::Generator && !overlay_) placeGenerator();
}
// The generator's blocks at the order's spans, in every preset (Legacy shows the preview and the
// keys too): the frequency block's title, digits, unit and Applied rows at fixed offsets in it, each
// within it; the preview the whole preview block. A theme's x, widths, fonts and colours stay; its
// y and the keys' theme y give way to the order.
void ScreenHost::placeGenerator() {
    ScreenLayout::generator(generatorOrder_, generatorSpans_);
    const auto *t = theme().elements[unsigned(MeasurementMode::Generator)];
    const auto &frequency = generatorSpans_[unsigned(GeneratorBlock::Frequency)], &preview = generatorSpans_[unsigned(GeneratorBlock::Preview)];
    const auto place = [&](ElementId id, int y, int height) {
        auto *object = elements_.get(id);
        const auto &style = t[unsigned(id)];
        lv_obj_set_pos(object, style.x, y);
        lv_obj_set_size(object, style.width, height);
        lv_obj_set_hidden(object, false);
    };
    const auto in = [&](ElementId id, int offset) { // The theme's height, never past the block's end.
        const int y = frequency.y + offset;
        place(id, y, std::min<int>(t[unsigned(id)].height, frequency.bottom() + 1 - y));
    };
    in(ElementId::Details, ScreenLayout::GeneratorTitle);
    in(ElementId::Reading, ScreenLayout::GeneratorDigits);
    in(ElementId::Unit, ScreenLayout::GeneratorUnit);
    in(ElementId::Measurements, ScreenLayout::GeneratorApplied);
    place(ElementId::Preview, preview.y, preview.height);
    buttons(generatorSpans_[unsigned(GeneratorBlock::Buttons)].y);
    lv_obj_set_hidden(elements_.get(ElementId::Parameters), false);
}
void ScreenHost::overlay(bool parameters) {
    overlay_ = true;
    lv_obj_set_hidden(meterPanel_, true); // A menu over the DMM: no dial work underneath it.
    functionIcon_.visible(false);
    dial_.visible(false);
    for (unsigned i = 0; i < ElementCount; ++i) {
        const auto id = ElementId(i);
        lv_obj_set_hidden(elements_.get(id), id != ElementId::Status && !(parameters && id == ElementId::Parameters));
    }
    decodeTable_.hide(); decodeStrip_.hide();
    if (parameters) buttons(ScreenLayout::MenuKeysY); // Over a menu: the same bar, at the bottom over the keys.
}
} // namespace openosc::ui
