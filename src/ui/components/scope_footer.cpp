#include "openosc/ui/components/scope_footer.hpp"
#include "openosc/ui/scope/display_scale.hpp"
#include "openosc/ui/scope/format.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::ui {
namespace {
using Layout = ScopeFooterLayout;
// Shares of a channel's colour over the background: the panel's fill, its frame, a tag.
constexpr uint8_t Fill = 28, Frame = 96, Tag = 140;
uint32_t mix(uint32_t color, uint32_t background, uint8_t share) {
    return lv_color_to_u32(lv_color_mix(lv_color_hex(color), lv_color_hex(background), share)) & 0xffffff;
}
// A light background (luma over half): the dimmed tag would wash out to a pale tint there.
bool light(uint32_t background) {
    const unsigned r = background >> 16 & 0xff, g = background >> 8 & 0xff, b = background & 0xff;
    return 299 * r + 587 * g + 114 * b > 127500;
}
lv_obj_t *box(lv_obj_t *parent) {
    auto *object = lv_obj_create(parent);
    lv_obj_remove_style_all(object);
    lv_obj_set_scrollable(object, false);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    return object;
}
} // namespace
void ScopeFooter::create(lv_obj_t *parent, const Theme &theme) {
    theme_ = &theme;
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_size(root_, StatusBarLayout::Width, Layout::Height);
    lv_obj_set_scrollable(root_, false);
    for (unsigned c = 0; c < Panels; ++c) panels_[c].create(root_, int(c));
    for (auto *&dot : dots_) {
        dot = box(root_);
        lv_obj_set_size(dot, Layout::PageDot, Layout::PageDot);
    }
    model_ = nullptr; resultValid_ = countersValid_ = false; indicator_ = {};
    replan(); paint();
}
void ScopeFooter::clear() {
    model_ = nullptr; resultValid_ = false; indicator_ = {};
    replan(); paint();
}
void ScopeFooter::selection(const scope::ReadoutSelection &selection) {
    selection_ = selection; resultValid_ = false; indicator_ = {}; // Until ReadoutScheduler publishes for it.
    replan(); paint();
}
void ScopeFooter::frame(const plot::Model &model) {
    const bool first = model_ != &model;
    model_ = &model;
    // Readout cells change with a result; the scales and the spectrum with every frame.
    if (!replan() && !first && pages_.count() && pages_.kind() == FooterPages::Kind::Readouts) return;
    paint();
}
void ScopeFooter::readouts(const scope::ReadoutResult &result, const ReadoutState &state) {
    result_ = result; resultValid_ = true;
    indicator_ = ReadoutIndicator::of(result.basis, state);
    paint();
}
void ScopeFooter::counters(const scope::CounterSnapshot &counters) {
    counters_ = counters; countersValid_ = true;
    replan(); paint();
}
void ScopeFooter::view(const plot::ChannelView &view) {
    if (view_ == view) return;
    view_ = view; pages_.view(view);
    paint();
}
void ScopeFooter::meter(const plot::MeterReadouts &meter) {
    if (meter == meter_) return;
    meter_ = meter;
    if (replan() || meterShown()) paint();
}
bool ScopeFooter::meterShown() const {
    if (!pages_.count()) return false;
    if (pages_.kind() == FooterPages::Kind::Scales) return pages_.meterPanel() < Panels;
    if (pages_.kind() != FooterPages::Kind::Readouts) return false;
    for (unsigned p = 0; p < Panels; ++p)
        if (pages_.cell(p * PerChannel).channel == FooterPages::Meter) return true;
    return false;
}
void ScopeFooter::nextPage() {
    pages_.next();
    paint();
}
bool ScopeFooter::replan() {
    bool enabled[2] = {true, true};
    bool spectrum = false, scales = false;
    if (model_) {
        for (unsigned c = 0; c < 2; ++c) enabled[c] = model_->metadata().channels[c].enabled;
        spectrum = model_->options().mode == plot::Mode::Spectrum;
        scales = model_->options().showScale;
    } else if (countersValid_) for (unsigned c = 0; c < 2; ++c) enabled[c] = counters_.enabled & (1U << c);
    return pages_.plan(selection_, enabled, spectrum, scales, meter_.shown);
}
bool ScopeFooter::enabled(unsigned channel) const {
    return model_ ? model_->metadata().channels[channel].enabled : !countersValid_ || (counters_.enabled & (1U << channel));
}
uint32_t ScopeFooter::channelColor(unsigned channel) const {
    if (channel == FooterPages::Meter) return meterColor_;
    return !enabled(channel) ? theme_->muted : view_.slot(channel) ? theme_->channel2 : theme_->channel1;
}
void ScopeFooter::look(unsigned channel, const uint32_t *color) {
    const uint32_t tint = color ? *color : theme_->grid;
    panels_[channel].begin(mix(tint, theme_->background, color ? Fill : 0), color ? mix(tint, theme_->background, Frame) : tint);
}
void ScopeFooter::describe(char *output, size_t capacity) const {
    if (!capacity) return;
    size_t used = 0;
    *output = 0;
    for (const auto &panel : panels_) {
        if (lv_obj_is_hidden(panel.object())) continue;
        for (unsigned i = 0; i < panel.count() && used < capacity; ++i) {
            const int n = std::snprintf(output + used, capacity - used, "%s|", panel.text(i).text);
            if (n < 0) return;
            used += size_t(n);
        }
    }
}
void ScopeFooter::paint() {
    if (!root_) return;
    for (unsigned c = 0; c < Panels; ++c) look(c, nullptr);
    const unsigned count = pages_.count();
    if (count) switch (pages_.kind()) {
    case FooterPages::Kind::Readouts: readoutPage(); break;
    case FooterPages::Kind::Scales: scalesPage(); break;
    case FooterPages::Kind::Spectrum: spectrumPage(); break;
    }
    for (auto &panel : panels_) panel.commit(count != 0);
    // A dot per page, the one on show bright; none when there is only one.
    const int top = (Layout::Height - int(count) * Layout::PageDotStep + (Layout::PageDotStep - Layout::PageDot)) / 2;
    for (unsigned i = 0; i < FooterPages::MaximumPages; ++i) {
        lv_obj_set_hidden(dots_[i], count < 2 || i >= count);
        lv_obj_set_pos(dots_[i], Layout::Pager.x, top + int(i) * Layout::PageDotStep);
        lv_obj_set_style_bg_color(dots_[i], lv_color_hex(i == pages_.page() ? theme_->foreground : theme_->grid), 0);
    }
}
void ScopeFooter::readoutPage() {
    for (unsigned c = 0; c < Panels; ++c) readoutPanel(c);
}
// The hardware counter's slots read the last snapshot even before a frame; the computed values
// wait for a frame and its result; the DMM trace's are its own (meter()), in its unit.
// p: the panel; its channel is the cells' (the one channel on fills both; the DMM trace may too).
void ScopeFooter::readoutPanel(unsigned p) {
    const unsigned c = pages_.cell(p * PerChannel).channel;
    const bool meter = c == FooterPages::Meter;
    const uint32_t color = channelColor(c);
    look(p, &color);
    if (pages_.cell(p * PerChannel).slot == FooterPages::ChannelOff) {
        if (model_) { char text[16]; std::snprintf(text, sizeof text, "CH%u Off", view_.number(c)); panels_[p].add(0, text, theme_->muted); }
        return;
    }
    ReadoutRow::Item items[ReadoutRow::Items];
    const bool computed = !meter && resultValid_ && result_.valid && result_.computed[c];
    for (unsigned k = 0; k < PerChannel; ++k) {
        const auto place = pages_.cell(p * PerChannel + k);
        if (place.slot == FooterPages::Empty || place.slot == FooterPages::ChannelOff) continue;
        auto &item = items[k];
        const unsigned kind = meter ? selection_.meter[place.slot] : selection_.values[c][place.slot];
        if (meter) {
            if (!meter_.offers(scope::Measurement(kind))) continue; // RMS of an AC function's readings: its cell stays empty.
            item.valid = meter_.value(scope::Measurement(kind), item.value);
            item.unit = meter_.unit;
        } else if (kind == unsigned(scope::Measurement::CounterFrequency)) {
            if (!countersValid_) continue;
            const auto &reading = counters_.channels[c];
            item.valid = reading.valid && (counters_.enabled & (1U << c));
            item.value = reading.hertz;
            // f: an explicitly filtered count; ?: unverified, uncertain, changing, or apart from the record.
            const bool doubtful = reading.quality != scope::CounterQuality::Clean && reading.quality != scope::CounterQuality::Filtered;
            const bool suspect = doubtful || (computed && scope::CounterCheck::suspect(reading, result_.values[c], result_.timestampUs));
            if (item.valid) item.mark = reading.quality == scope::CounterQuality::Filtered ? (suspect ? "f?" : "f") : suspect ? "?" : "";
        } else {
            if (!model_) continue;
            const auto measurement = scope::Measurement(kind);
            item.valid = computed && result_.values[c].value(measurement, item.value);
            if (item.valid) {
                const auto shown = ReadoutText::presentation(result_.values[c], measurement);
                item.mark = shown.mark; item.limit = shown.limit;
            }
        }
        item.kind = kind;
        if (held_[p][k].kind == kind && heldChannel_[p] == c) item.prefer = held_[p][k].prefix;
    }
    heldChannel_[p] = c;
    ReadoutRow::Placed placed[ReadoutRow::Items];
    ReadoutRow::fit(items, Layout::TextWidth, Layout::CellWidth, placed, &held_[p]);
    // A stale Full result: both shades go grey; the counter and the DMM trace have their own data and
    // keep their colour.
    for (unsigned k = 0; k < PerChannel; ++k) {
        if (!placed[k].shown) continue;
        const bool apart = meter || items[k].kind == unsigned(scope::Measurement::CounterFrequency);
        const uint32_t value = indicator_.stale && !apart ? theme_->muted : color;
        const auto &parts = placed[k].parts;
        // A tag is its value's colour dimmed towards a dark background; on a light one it keeps the
        // full (dark) colour, so the names stay as readable as the values.
        panels_[p].add(placed[k].x, parts.tag, light(theme_->background) ? value : mix(value, theme_->background, Tag), parts.size);
        panels_[p].add(placed[k].x + parts.tagWidth, parts.value, value, parts.size);
    }
    // Full: the bottom rail, whole (a first pass: as far as it has come); grey with stale values.
    if (indicator_.full && model_ && !meter && enabled(c))
        panels_[p].rail(Layout::rail(int(p)).width * indicator_.percent / 100, indicator_.stale ? theme_->muted : color);
}
const char *ScopeFooter::samples(const plot::Model &model, unsigned channel) {
    const auto &options = model.options();
    if (options.averaging > 1) return " Avg"; // Frame averaging draws the mean over any Samples.
    if (options.reduction == plot::Reduction::Peak) return "";
    if (options.reduction == plot::Reduction::Adaptive) return model.effectiveMean(channel) ? " aHD" : " aPk";
    return model.effectiveMean(channel) ? " HD" : "";
}
void ScopeFooter::scalesPage() {
    if (!model_) return;
    const auto &metadata = model_->metadata();
    const bool xy = model_->options().mode == plot::Mode::XY;
    for (unsigned p = 0; p < 2; ++p) { // A panel per on-screen slot; X and Y stay the inputs' (X-Y).
        const unsigned c = view_.input(p);
        const auto &channel = metadata.channels[c];
        char text[48], probe[24];
        if (p == pages_.meterPanel()) { // The DMM trace in the panel of a channel that is off.
            look(p, &meterColor_);
            meterScale(text, sizeof text);
            panels_[p].add(0, text, meterColor_);
            continue;
        }
        const uint32_t color = channelColor(c);
        look(p, &color);
        if (!channel.enabled) std::snprintf(text, sizeof text, "CH%u Off", view_.number(c));
        else {
            const auto scale = plot::DisplayScale::channel(channel, model_->options().channels[c]);
            plot::DisplayScale::probe(channel, probe, sizeof probe);
            char tag[40];
            std::snprintf(tag, sizeof tag, "%sCH%u %s %s%s", xy ? (c ? "Y " : "X ") : "", view_.number(c), plot::DisplayScale::coupling(channel), probe, xy ? "" : samples(*model_, c));
            ReadoutText::fit(tag, true, scale.perDivision, scale.unit, Layout::TextWidth, text, sizeof text);
        }
        panels_[p].add(0, text, color);
    }
}
// "DMM", its function and units/div, as a channel's coupling, probe and V/div; without the function
// where that does not fit.
void ScopeFooter::meterScale(char *text, size_t capacity) const {
    char tag[40];
    std::snprintf(tag, sizeof tag, "DMM %s", meter_.function);
    const bool scaled = meter_.perDivision > 0;
    if (!ReadoutText::fit(tag, scaled, meter_.perDivision, meter_.unit, Layout::TextWidth, text, capacity))
        ReadoutText::fit("DMM", scaled, meter_.perDivision, meter_.unit, Layout::TextWidth, text, capacity);
}
// The plot's own header names the channel, window and length: the footer adds the peak (in the
// spectrum channel's panel) and the frequency span of a division (in a plain one).
void ScopeFooter::spectrumPage() {
    if (!model_) return;
    const auto &spectrum = model_->spectrum();
    const uint32_t color = view_.slot(model_->options().spectrum.channel) ? theme_->channel2 : theme_->channel1;
    char text[48];
    look(0, &color);
    ReadoutText::fit("Pk", spectrum.hasPeak, spectrum.peakHz, "Hz", Layout::TextWidth, text, sizeof text);
    panels_[0].add(0, text, color);
    ReadoutText::fit("", true, (model_->spectrumFrequency(1000) - model_->spectrumFrequency(0)) / 10, "Hz", Layout::TextWidth, text, sizeof text, "/div");
    panels_[1].add(0, text, neutral());
}
} // namespace openosc::ui
