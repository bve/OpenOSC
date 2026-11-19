#include "openosc/ui/screens/scope_screen.hpp"
#include "openosc/ui/screens/screen_layout.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/scope/display_scale.hpp"
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
    channel_ = channel < 2 ? channel : 0;
    cards();
}
void ScopeScreen::context(unsigned channel, plot::Focus focus) {
    if (channel_ != channel) valid_ = false;
    channel_ = channel < 2 ? channel : 0;
    focus_ = focus;
    cards();
}
// The generator running beside the capture; while its DAC stream loads the bus, ", ADC max /2": the
// next captures' maximum sample rate is halved (a held record stays as it was captured).
void ScopeScreen::generator(const generator::Snapshot &output, bool captureHalved) {
    char text[64] = "", frequency[24];
    if (output.enabled && output.applied.actualHz.valid() && plot::Format::quantity(output.applied.actualHz.value(), "Hz", frequency, sizeof frequency, 3)) {
        const unsigned shape = unsigned(output.requested.shape);
        std::snprintf(text, sizeof text, "GEN %s %s%s", shape < 6 ? generator::ShapeNames[shape] : "?", frequency,
                      captureHalved ? ", ADC max /2" : "");
    }
    host_.waveform().note(text, host_.theme().generator);
}
void ScopeScreen::cards() {
    auto &bar = host_.parameters();
    const auto &widget = host_.waveform();
    const auto &model = widget.model();
    const bool framed = model.hasFrame();
    const auto &shown = framed ? model.metadata() : next_;
    const auto &selected = shown.channels[channel_];
    char scale[24], time[24], value[64], title[64], details[160];
    const auto scaleValue = plot::DisplayScale::channel(selected, framed ? model.options().channels[channel_] : plot::ChannelOptions{});
    plot::Format::quantity(scaleValue.perDivision, scaleValue.unit, scale, sizeof scale, 3); // The strip uses the same precision.
    const bool spectrum = widget.options().mode == plot::Mode::Spectrum;
    plot::Format::quantity(framed && !spectrum ? plot::DisplayScale::secondsPerDivision(model.viewport(), shown) : shown.timebaseNs / 1e9, "s", time, sizeof time, 3);
    std::snprintf(value, sizeof value, "%s %s", scale, time); // F1 is VOL/TIME, as on the factory firmware.
    char probe[24], next[48] = "";
    plot::DisplayScale::probe(selected, probe, sizeof probe);
    // F1 Scale: the value row is the shown record's V/div and s/div (what the plot is drawn at);
    // scales asked for but not yet captured (a stopped or held record) are its title, marked in the
    // highlight: only the scales that differ, as the first that fits the zone (12, else condensed 11):
    // "Scale Next 2V 20us", "Next 2V 20us", or "2V 20us" (the highlight still says Next).
    char volts[16] = "", seconds[16] = "";
    if (framed && plot::DisplayScale::nextDiffers(shown, next_, channel_)) {
        const auto scaleNext = plot::DisplayScale::channel(next_.channels[channel_], plot::ChannelOptions{});
        plot::Format::quantity(scaleNext.perDivision, scaleNext.unit, volts, sizeof volts, 3);
        plot::Format::quantity(next_.timebaseNs / 1e9, "s", seconds, sizeof seconds, 3);
        char parts[40];
        const bool voltsDiffer = std::strcmp(volts, scale) != 0, timeDiffers = std::strcmp(seconds, time) != 0;
        std::snprintf(parts, sizeof parts, "%s%s%s", voltsDiffer || !timeDiffers ? volts : "",
                      (voltsDiffer || !timeDiffers) && timeDiffers ? " " : "", timeDiffers ? seconds : "");
        std::snprintf(next, sizeof next, "Next %s", parts);
        const int room = ScreenLayout::ButtonWidth - 8;
        const auto fits = [&](const char *text) { return Elements::textWidth(text, 12) <= room || Elements::textWidth(text, 11) <= room; };
        std::snprintf(title, sizeof title, "Scale %s", next);
        if (!fits(title)) std::snprintf(title, sizeof title, "%s", fits(next) ? next : parts);
    } else std::snprintf(title, sizeof title, "Scale CH%u", channel_ + 1);
    bar.item(0, title, value, *next);
    // F2 Position: the channel it moves, with its coupling and probe.
    std::snprintf(value, sizeof value, "CH%u %s %s", channel_ + 1, plot::DisplayScale::coupling(selected), probe);
    bar.item(1, "Position", value);
    static constexpr const char *modes[] = {"AUTO", "NORM", "SNGL"};
    std::snprintf(value, sizeof value, "CH%u %s", unsigned(model.triggerChannel()) + 1, modes[unsigned(triggerMode_) % 3]);
    bar.item(2, "Trigger", value);
    if (focus_ == plot::Focus::Cursor && model.cursorFocus() != plot::CursorLine::None)
        bar.item(3, "Cursor", plot::CursorControl::lineName(model.cursorFocus(), spectrum));
    else
        bar.item(3, "Analyze", widget.options().mode == plot::Mode::XY ? "XY" : spectrum ? "FFT" : widget.options().mode == plot::Mode::SpectrumOverlay ? "Y-T+FFT" : "Display");
    // Selected, each card takes the colour of what it acts on (issue #21): F1 Scale and F2 Position
    // the selected channel's, F3 Trigger the source channel's, F4 Analyze/Cursor neutral.
    using Tint = ParameterBar::Tint;
    const auto channelTint = [](unsigned c) { return c == 0 ? Tint::Channel1 : c == 1 ? Tint::Channel2 : Tint::Neutral; };
    bar.tint(0, channelTint(channel_));
    bar.tint(1, channelTint(channel_));
    bar.tint(2, channelTint(unsigned(model.triggerChannel())));
    bar.tint(3, Tint::Neutral);
    if (!host_.overlayActive()) bar.select(unsigned(focus_));
    std::snprintf(details, sizeof details, "CH%u %s %s  %s %s%s%s", channel_ + 1, plot::DisplayScale::coupling(selected), probe, scale, time, *next ? "  " : "", next);
    host_.elements().text(ElementId::Details, details);
}
void ScopeScreen::update(const scope::FrameView &frame, bool held, unsigned selected, DataStatus health) {
    if (!frame.valid() || selected > 1 || host_.overlayActive()) return;
    auto &widget = host_.waveform();
    widget.held(held);
    // A new frame for TRIG'D only when it is another record (or the first: sequence 0 included); the
    // controller passes the same leased record again for UI refreshes, which must not renew TRIG'D.
    const auto &m = frame.metadata();
    newFrame_ = !frameSeen_ || m.sequence != frameSequence_ || m.timestampUs != frameTimestampUs_;
    frameSeen_ = true; frameSequence_ = m.sequence; frameTimestampUs_ = m.timestampUs;
    widget.setFrame(frame);
    status(health, held);

    host_.scopeFooter().frame(widget.model()); // Every preset: the footer's six readouts (Legacy too).
    cards();
    if (valid_ && held_ == held && channel_ == selected) return;
    valid_ = true;
    held_ = held;
    channel_ = selected;
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
