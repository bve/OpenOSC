#pragma once
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/components/footer_pages.hpp"
#include "openosc/ui/components/footer_panel.hpp"
#include "openosc/ui/components/readout_row.hpp"
#include "openosc/ui/scope/meter_readouts.hpp"
#include "openosc/ui/scope/model.hpp"

namespace openosc::ui {
// The Scope footer row (ScopeFooterLayout): the page FooterPages plans, painted from the last
// frame, readout result, counter snapshot and DMM trace readouts it was given, so turning a page needs
// no new data. Each panel: a faint fill and frame in its channel's colour (the DMM trace's in its
// own), its readouts (ReadoutRow) as a dimmed tag beside a bright value. The basis has no text:
// ReadoutIndicator lights a channel panel's bottom rail for Full and dims values that went stale;
// Screen shows nothing.
class ScopeFooter {
public:
    void create(lv_obj_t *parent, const Theme &theme);
    // The colours to paint with (a Scope appearance's theme, whose contents may change in place, and
    // its DMM role): repainted at once, the readouts, page and record state kept.
    void theme(const Theme &theme, uint32_t meter) { theme_ = &theme; meterColor_ = meter; paint(); }
    void visible(bool value) { lv_obj_set_hidden(root_, !value); }
    // Swap CH1/CH2 (plot::ChannelView): each input's panel, colour and number; repainted at once.
    void view(const plot::ChannelView &view);
    void clear(); // No frame on display: no scales, values or state.
    void selection(const scope::ReadoutSelection &selection);
    void frame(const plot::Model &model); // Each painted frame: the scales and spectrum pages.
    void readouts(const scope::ReadoutResult &result, const ReadoutState &state);
    void counters(const scope::CounterSnapshot &counters);
    // The DMM trace, each display pass: repainted when it changes what the footer plans or shows.
    void meter(const plot::MeterReadouts &meter);
    void nextPage();
    const FooterPages &pages() const { return pages_; }
    // What the rails and value shades show now (Screen until a Full result is painted).
    const ReadoutIndicator &indicator() const { return indicator_; }
    const FooterPanel &panel(unsigned channel) const { return panels_[channel]; }
    // Every text on show, the on-screen CH1's panel first, each followed by '|' ("f|19.9kHz|Vpp|5.5V|...").
    void describe(char *output, size_t capacity) const;
    // The Scales page's note of what a channel's columns show, when not the default Peak: " Avg"
    // (frame averaging, over any Samples), " HD", or Samples Auto's choice now (" aHD"/" aPk").
    static const char *samples(const plot::Model &model, unsigned channel);

private:
    static constexpr unsigned Panels = ScopeFooterLayout::Panels, PerChannel = ScopeFooterLayout::PerChannel;
    ReadoutRow::Held held_[ScopeFooterLayout::Panels][ReadoutRow::Items]; // Each cell's last form and prefix (hysteresis).
    unsigned heldChannel_[ScopeFooterLayout::Panels]{};
    bool replan(); // True when the pages changed.
    void paint();
    void readoutPage();
    void readoutPanel(unsigned panel);
    void scalesPage();
    void spectrumPage();
    bool meterShown() const; // The page on show has a DMM trace cell or panel.
    void meterScale(char *text, size_t capacity) const; // The DMM trace's Scales panel text.
    // A panel's look: tinted in `color` (a channel's), or plain (theme grid) for no channel.
    void look(unsigned channel, const uint32_t *color);
    bool enabled(unsigned channel) const;
    uint32_t channelColor(unsigned channel) const;
    lv_obj_t *root_ = nullptr, *dots_[FooterPages::MaximumPages]{};
    FooterPanel panels_[Panels];
    const Theme *theme_ = nullptr;
    uint32_t meterColor_ = 0; // The Scope appearance's DMM role.
    plot::MeterReadouts meter_;
    // Neutral readout text: the Scope's readouts element colour (the appearance's Readout role).
    uint32_t neutral() const { return theme_->elements[unsigned(MeasurementMode::Oscilloscope)][unsigned(ElementId::Measurements)].color; }
    const plot::Model *model_ = nullptr;
    scope::ReadoutSelection selection_;
    scope::ReadoutResult result_;
    ReadoutIndicator indicator_;
    scope::CounterSnapshot counters_;
    bool resultValid_ = false, countersValid_ = false;
    FooterPages pages_;
    plot::ChannelView view_;
};
} // namespace openosc::ui
