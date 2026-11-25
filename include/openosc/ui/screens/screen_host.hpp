#pragma once
#include "openosc/ui/components/generator_preview.hpp"
#include "openosc/ui/components/parameter_bar.hpp"
#include "openosc/ui/components/meter_summary.hpp"
#include "openosc/ui/components/scope_footer.hpp"
#include "openosc/ui/components/meter_trend.hpp"
#include "openosc/ui/components/analog_dial.hpp"
#include "openosc/ui/components/function_icon.hpp"
#include "openosc/ui/components/meter_cards.hpp"
#include "openosc/ui/components/status_bar_layout.hpp"
#include "openosc/ui/components/decode_table.hpp"
#include "openosc/ui/components/decode_strip.hpp"
#include "openosc/ui/screens/screen_layout.hpp"
#include "openosc/ui/components/scope_styles.hpp"
#include "openosc/ui/waveform.hpp"

namespace openosc::ui {
// A single retained LVGL tree is shared by the three screen presenters.
enum class StatusTone : uint8_t { Neutral, Good, Warning, Alert };
class ScreenHost {
public:
    void status(const char *text, StatusTone tone); // The coloured state pill, right of the battery.
    void statusX(int x);
    int statusRight() const { return statusRight_; } // First pixel column right of the pill.
    void clear() { if (root_) lv_obj_delete(root_); }
    void build(lv_obj_t *parent, const Theme &theme);
    void activate(MeasurementMode mode);
    // The Scope's block order (ScreenLayout::scope): applied at once when the Scope shows, else on
    // its next activation; the same order changes nothing. Only y and heights move: the waveform,
    // its record and the footer are the same objects.
    void scopeArrangement(const ScopeArrangement &order);
    const ScopeArrangement &scopeArrangement() const { return scopeOrder_; }
    const BlockSpan &scopeSpan(ScopeBlock block) const { return scopeSpans_[unsigned(block)]; }
    // The generator's block order (ScreenLayout::generator), the same way: at once when it shows.
    void generatorArrangement(const GeneratorArrangement &order);
    const GeneratorArrangement &generatorArrangement() const { return generatorOrder_; }
    const BlockSpan &generatorSpan(GeneratorBlock block) const { return generatorSpans_[unsigned(block)]; }
    // The soft keys at `y` (x, width and height are ScreenLayout's, the same on every screen).
    void buttons(int y);
    void overlay(bool parameters);
    // The theme in force: on the Scope its appearance's (scopeTheme), elsewhere the base one.
    const Theme &theme() const { return mode_ == MeasurementMode::Oscilloscope && scopeTheme_ ? *scopeTheme_ : *theme_; }
    // The Scope's own colours (ScopeStyles::effective of the base theme): repainted at once when the
    // Scope shows (no object rebuilt: the waveform's record, HOLD, zoom and the readouts stay).
    // `look`: the roles the theme has no field for (Trigger, TagText, Readout on the plot).
    void scopeTheme(const Theme *theme, const ScopeLook *look, bool apply = true); // apply false: only kept (a build: activate() follows).
    // Settings > Display > Swap CH1/CH2 for the Scope's plot and footer (plot::ChannelView): repainted
    // at once, a held record included; acquisition, options and readouts stay as they are.
    void channelView(const plot::ChannelView &view) { channelView_ = view; waveform_.channelView(view); scopeFooter_.view(view); }
    const plot::ChannelView &channelView() const { return channelView_; }
    StatusTone tone_ = StatusTone(255);
    Elements &elements() { return elements_; }
    ParameterBar &parameters() { return parameters_; }
    WaveformWidget &waveform() { return waveform_; }
    const WaveformWidget &waveform() const { return waveform_; }
    GeneratorPreview &preview() { return preview_; }
    MeterSummary &meterSummary() { return meterSummary_; }
    ScopeFooter &scopeFooter() { return scopeFooter_; }
    const ScopeFooter &scopeFooter() const { return scopeFooter_; }
    MeterTrend &meterTrend() { return meterTrend_; }
    FunctionIcon &functionIcon() { return functionIcon_; }
    MeterCards &meterCards() { return meterCards_; }
    AnalogDial &dial() { return dial_; }
    lv_obj_t *meterPanel() const { return meterPanel_; } // The DMM body's background, behind every element.
    bool overlayActive() const { return overlay_; }
    // The protocol decoder's view on the Scope (plan §4.1, §4.2): Split and Decoder only take the
    // decoder's own layout for as long as they show (scopeOrder_ is never changed: it returns as it
    // was), Wave + annotations the user's order with the strip over the plot's bottom rows. Applied
    // at once when the Scope shows; the same state changes nothing.
    void decoder(bool shown, decoding::View view);
    bool decoderShown() const { return decoderShown_; }
    // Whether the trace is drawn: no menu over the Scope, and not Decoder only (plan §4.4).
    bool traceDrawn() const { return !overlay_ && !(decoderShown_ && decoderView_ == decoding::View::Table); }
    DecodeTable &decodeTable() { return decodeTable_; }
    DecodeStrip &decodeStrip() { return decodeStrip_; }
    const DecodeTable &decodeTable() const { return decodeTable_; }
    const DecodeStrip &decodeStrip() const { return decodeStrip_; }

private:
    void placeStatus();
    void placeScope(); // The Scope's blocks at their spans for scopeOrder_.
    void placeGenerator(); // The generator's blocks at their spans for generatorOrder_.
    void paintScope(); // The Scope's colours from theme(): waveform, footer, chrome.
    lv_obj_t *root_ = nullptr, *chrome_ = nullptr, *meterPanel_ = nullptr;
    int statusX_ = StatusBarLayout::StatusX, statusWidth_ = 0, statusRight_ = 0;
    bool overlay_ = false, decoderShown_ = false;
    decoding::View decoderView_ = decoding::View::Table;
    MeasurementMode mode_ = MeasurementMode::Oscilloscope;
    ScopeArrangement scopeOrder_;
    BlockSpan scopeSpans_[ScopeArrangement::Size];
    GeneratorArrangement generatorOrder_;
    BlockSpan generatorSpans_[GeneratorArrangement::Size];
    const Theme *theme_ = nullptr, *scopeTheme_ = nullptr;
    const ScopeLook *scopeLook_ = nullptr;
    plot::ChannelView channelView_;
    Elements elements_;
    ParameterBar parameters_;
    WaveformWidget waveform_;
    GeneratorPreview preview_;
    MeterSummary meterSummary_;
    ScopeFooter scopeFooter_;
    MeterTrend meterTrend_;
    FunctionIcon functionIcon_;
    MeterCards meterCards_;
    AnalogDial dial_;
    DecodeTable decodeTable_;
    DecodeStrip decodeStrip_;
};
} // namespace openosc::ui
