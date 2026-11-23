#include "openosc/ui/screens/scope_screen.hpp"
#include "openosc/ui/screens/screen_layout.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/scope/display_scale.hpp"
#include "openosc/ui/scope/view_zoom.hpp"
#include "openosc/ui/scope/plot_frame.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/acquisition/readouts.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::ui {
void ScopeScreen::activate(const ApplicationState &state) {
    measurements_=state.scope.measurements.valid() ? state.scope.measurements : scope::ReadoutSelection{};
    host_.scopeFooter().selection(measurements_);
    countersValid_=false; readoutsPainted_=false;
    valid_ = false;
    status_ = "WAITING FOR DATA";
    automatic_=calibrating_=false;
    host_.status(status_, StatusTone::Warning);
    // Closing a panel reactivates this screen: a trace still on display keeps its labels.
    const bool traced=host_.waveform().model().hasFrame();
    host_.elements().text(ElementId::Measurements, traced ? "" : "No completed capture");
    if (!traced) {
        host_.scopeFooter().clear();
        scope::FrameMetadata next;
        next.timebaseNs = state.scope.timebaseNs;
        for (unsigned c = 0; c < 2; ++c) { next.channels[c].millivoltsPerDivision = state.scope.millivoltsPerDivision[c]; next.channels[c].calibrated = true; }
        next_ = next;
    }
    cards();
}
void ScopeScreen::settings(const scope::FrameMetadata &next, unsigned channel) {
    next_ = next;
    select(channel);
    cards();
}
void ScopeScreen::context(unsigned channel, plot::Focus focus, bool panReverse) {
    if (channel_ != channel) valid_ = false;
    select(channel);
    focus_ = focus; panReverse_ = panReverse;
    cards();
    if (focus_ == plot::Focus::Zoom) zoomNote();
}
void ScopeScreen::meter(const Meter &meter) {
    if (meter.selected == meter_.selected && meter.perDivision == meter_.perDivision && !std::strcmp(meter.unit, meter_.unit) &&
        !std::strcmp(meter.function, meter_.function))
        return;
    meter_ = meter;
    cards();
}
void ScopeScreen::meterReadouts(const plot::MeterReadouts &readouts) {
    if (!host_.overlayActive()) host_.scopeFooter().meter(readouts);
}
void ScopeScreen::select(unsigned channel) {
    channel_ = channel < 2 ? channel : 0;
    host_.waveform().frontChannel(channel_);
}
// The generator running beside the capture; while its DAC stream loads the bus, ", ADC max /2": the
// next captures' maximum sample rate is halved (a held record stays as it was captured). A view away
// from home (zoom, pan) keeps its marker in front of it (issue #111), in the GEN note's colour when
// both are on; a GEN note too wide for the plot beside it leaves out the ADC remark, then the shape.
void ScopeScreen::generator(const generator::Snapshot &output, bool captureHalved) {
    if (focus_ == plot::Focus::Zoom) { zoomNote(); return; }
    auto &widget = host_.waveform();
    char text[64], frequency[24]; // The view marker first, the GEN note after it: no buffer of its own (stack).
    plot::ViewZoom::marker(widget.options(), widget.model(), text, sizeof text);
    const size_t view = std::strlen(text);
    const bool gen = output.enabled && output.applied.actualHz.valid() &&
                     plot::Format::quantity(output.applied.actualHz.value(), "Hz", frequency, sizeof frequency, 3);
    const unsigned shape = unsigned(output.requested.shape);
    // The note's room: the plot left of its anchor 30 px inside the right margin, less its padding.
    const int width = widget.model().viewport().width ? int(widget.model().viewport().width) : plot::PlotFrame::PlotWidth;
    const int room = width - (plot::PlotFrame::Right + 30) - plot::PlotFrame::Left - 4;
    for (unsigned detail = 3; detail-- > 0;) { // GEN as it is, without ", ADC max /2", without the shape.
        if (!gen) break;
        std::snprintf(text + view, sizeof text - view, "%sGEN %s%s%s%s", view ? "  " : "", detail < 1 ? "" : shape < 6 ? generator::ShapeNames[shape] : "?",
                      detail >= 1 ? " " : "", frequency, detail >= 2 && captureHalved ? ", ADC max /2" : "");
        if (!view || Elements::textWidth(text, 12) <= room) break;
    }
    widget.note(text, gen ? host_.theme().generator : host_.theme().cursor);
}
// Zoom & Pan (issue #96): its factor and what the arrows do, in place of the GEN note.
void ScopeScreen::zoomNote() {
    auto &widget = host_.waveform();
    char factor[16], text[64];
    plot::ViewZoom::shownFactor(widget.options(), widget.model(), factor, sizeof factor);
    std::snprintf(text, sizeof text, "ZOOM %s  Up/Dn zoom  L/R %s", factor, panReverse_ ? "later/earlier" : "earlier/later");
    widget.note(text, host_.theme().cursor);
}
void ScopeScreen::cards() {
    auto &bar = host_.parameters();
    const auto &widget = host_.waveform();
    const auto &model = widget.model();
    const bool framed = model.hasFrame();
    const auto &shown = framed ? model.metadata() : next_;
    const auto &selected = shown.channels[channel_];
    const auto &view = host_.channelView(); // Each card names and tints its input by its on-screen slot.
    char scale[24], time[24], value[64], title[64], details[160];
    // The DMM trace selected (F1/F2): its units/div in place of the channel's V/div.
    const bool meter = meter_.selected;
    auto scaleValue = plot::DisplayScale::channel(selected, framed ? model.options().channels[channel_] : plot::ChannelOptions{});
    if (meter) scaleValue = {meter_.perDivision, meter_.unit};
    plot::Format::quantity(scaleValue.perDivision, scaleValue.unit, scale, sizeof scale, 3); // The strip uses the same precision.
    const bool spectrum = widget.options().mode == plot::Mode::Spectrum;
    const double secondsPerDivision = framed && !spectrum
        ? plot::DisplayScale::secondsPerDivision(model.viewport(), shown) : shown.timebaseNs / 1e9;
    plot::Format::quantity(secondsPerDivision, "s", time, sizeof time, 3);
    char probe[24], next[48] = "";
    plot::DisplayScale::probe(selected, probe, sizeof probe);
    if (meter) std::snprintf(title, sizeof title, "Scale DMM");
    else std::snprintf(title, sizeof title, "Scale CH%u", view.number(channel_));
    // Keep the value row at the plotted scale. A waiting record can still be on screen after its
    // range changes. Show the next setting in the title until a record at that scale arrives.
    if (framed) {
        const bool volts = !meter && plot::DisplayScale::voltsPending(shown, widget.options(), next_, channel_);
        const bool seconds = plot::DisplayScale::timePending(shown, widget.options(), next_);
        char pendingVolts[16] = "", pendingTime[16] = "";
        if (volts) {
            const auto scaleNext = plot::DisplayScale::channel(next_.channels[channel_], plot::ChannelOptions{});
            plot::Format::quantity(scaleNext.perDivision, scaleNext.unit, pendingVolts, sizeof pendingVolts, 3);
        }
        if (seconds) plot::Format::quantity(next_.timebaseNs / 1e9, "s", pendingTime, sizeof pendingTime, 3);
        if (volts || seconds) {
            std::snprintf(next, sizeof next, "Next %s%s%s", pendingVolts, volts && seconds ? " " : "", pendingTime);
            const char *first = volts ? pendingVolts : pendingTime;
            if (Elements::textWidth(next, 12) <= ScreenLayout::ButtonWidth - 8)
                std::snprintf(title, sizeof title, "%s", next);
            else
                std::snprintf(title, sizeof title, "Next %s", first);
        }
    }
    if (meter) std::snprintf(details, sizeof details, "DMM %s  %s %s%s%s", meter_.function, scale, time, *next ? "  " : "", next);
    else std::snprintf(details, sizeof details, "CH%u %s %s  %s %s%s%s", view.number(channel_), plot::DisplayScale::coupling(selected), probe, scale, time, *next ? "  " : "", next);
    // Keep the font fixed. A wide pair uses two significant digits, then coarser unit prefixes
    // if needed (for example, "500mcnt" becomes "0.5cnt").
    std::snprintf(value, sizeof value, "%s %s", scale, time);
    for (bool coarse : {false, true}) {
        if (Elements::textWidth(value, 11) <= ScreenLayout::ButtonWidth - 8) break;
        plot::Format::quantity(scaleValue.perDivision, scaleValue.unit, scale, sizeof scale, 2, coarse);
        plot::Format::quantity(secondsPerDivision, "s", time, sizeof time, 2, coarse);
        std::snprintf(value, sizeof value, "%s %s", scale, time);
    }
    bar.item(0, title, value, *next, true);
    // F2 Position: the channel it moves, with its coupling and probe; the DMM trace with its unit.
    const char *compactProbe = std::strcmp(probe, "x10000") == 0 ? "X10K" : probe;
    if (meter) std::snprintf(value, sizeof value, "DMM %s", meter_.unit);
    else std::snprintf(value, sizeof value, "CH%u %s %s", view.number(channel_), plot::DisplayScale::coupling(selected), compactProbe);
    bar.item(1, "Position", value);
    static constexpr const char *modes[] = {"AUTO", "NORM", "SNGL"};
    static constexpr const char *types[] = {"Trigger", "Pulse width", "Runt", "Rise/Fall"};
    // No channel shown (the DMM trace alone, plot::ChannelDisplay): Auto, of no channel.
    const bool source = plot::ChannelDisplay::shown(shown, widget.options(), 0) || plot::ChannelDisplay::shown(shown, widget.options(), 1);
    if (source) std::snprintf(value, sizeof value, "CH%u %s", view.number(unsigned(model.triggerChannel())), modes[unsigned(triggerMode_) % 3]);
    else std::snprintf(value, sizeof value, "%s", modes[0]);
    bar.item(2, types[unsigned(triggerType_)], value);
    if (focus_ == plot::Focus::Zoom) { // Zoom & Pan: F4 goes back to the card it came from.
        char factor[16];
        plot::ViewZoom::shownFactor(widget.options(), model, factor, sizeof factor);
        std::snprintf(value, sizeof value, "Zoom %s", factor);
        bar.item(3, "Back", value);
    } else if (focus_ == plot::Focus::Cursor && model.cursorFocus() != plot::CursorLine::None)
        bar.item(3, "Cursor", plot::CursorControl::lineName(model.cursorFocus(), spectrum));
    else
        bar.item(3, "Analyze", widget.options().mode == plot::Mode::XY ? "XY" : spectrum ? "FFT" : widget.options().mode == plot::Mode::SpectrumOverlay ? "Y-T+FFT" : "Display");
    // Selected, each card takes the colour of what it acts on (issue #21): F1 Scale and F2 Position
    // the selected channel's (the DMM trace's), F3 Trigger the source channel's, F4 Analyze/Cursor neutral.
    using Tint = ParameterBar::Tint;
    const auto channelTint = [&](unsigned c) { return c > 1 ? Tint::Neutral : view.slot(c) ? Tint::Channel2 : Tint::Channel1; };
    bar.tint(0, meter ? Tint::Meter : channelTint(channel_));
    bar.tint(1, meter ? Tint::Meter : channelTint(channel_));
    bar.tint(2, source ? channelTint(unsigned(model.triggerChannel())) : Tint::Neutral);
    bar.tint(3, Tint::Neutral);
    if (!host_.overlayActive()) bar.select(focus_ == plot::Focus::Zoom ? 3U : unsigned(focus_));
    host_.elements().text(ElementId::Details, details);
}
void ScopeScreen::update(const scope::FrameView &frame, bool held, unsigned selected, DataStatus health) {
    if (!frame.valid() || selected > 1 || host_.overlayActive()) return;
    auto &widget = host_.waveform();
    // A new frame for TRIG'D only when it is another record (or the first: sequence 0 included); the
    // controller passes the same leased record again for UI refreshes, which must not renew TRIG'D.
    const auto &m = frame.metadata();
    newFrame_ = !frameSeen_ || m.sequence != frameSequence_ || m.timestampUs != frameTimestampUs_;
    frameSeen_ = true; frameSequence_ = m.sequence; frameTimestampUs_ = m.timestampUs;
    // Single delivers a new acquisition already marked held. Count it before freezing the history.
    const bool freezeAfterFrame = held && newFrame_ && widget.options().graded();
    widget.held(held && !freezeAfterFrame);
    widget.setFrame(frame);
    if (freezeAfterFrame) widget.held(true);
    status(health, held);

    host_.scopeFooter().frame(widget.model()); // Every preset: the footer's six readouts (Legacy too).
    cards();
    if (focus_ == plot::Focus::Zoom) zoomNote(); // With the Back card: the factor this record shows.
    if (valid_ && held_ == held && channel_ == selected) return;
    valid_ = true;
    held_ = held;
    select(selected);
    host_.elements().text(ElementId::Measurements,"");
}
void ScopeScreen::nextFooterPage() {
    if (!host_.overlayActive()) host_.scopeFooter().nextPage();
}
// Values computed beside the waveform (ReadoutScheduler): never recomputed while painting a frame.
bool ScopeScreen::readouts(const scope::ReadoutResult &result, const ReadoutState &state) {
    if (!valid_ || host_.overlayActive()) return false;
    host_.scopeFooter().readouts(result, state);
    return readoutsPainted_ = true;
}
} // namespace openosc::ui
