#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/ui/scope/model.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
namespace {
constexpr int AnalogFine = 1, AnalogCoarse = 4, DigitalFine = 7, DigitalCoarse = 25;
constexpr int32_t PanStep = 25000000;
bool shiftAnalog(ScopeRequest &request, unsigned channel, int amount) {
    int &position = request.frontend.position[channel];
    const int next = std::clamp(position + amount, 0, 160);
    const bool changed = next != position;
    position = next;
    return changed;
}
bool shiftDigital(Options &options, unsigned channel, int amount) {
    auto &position = options.channels[channel].positionPermille;
    const auto next = int16_t(std::clamp<int>(position + amount, -1000, 2000));
    const bool changed = next != position;
    position = next;
    return changed;
}
} // namespace
int32_t TraceMove::panStep(const Model *model, bool fine) {
    if (!model) return PanStep;
    const auto &view = model->viewport();
    const uint32_t samples = model->metadata().samples;
    if (samples < 2 || !view.spanQ16 || view.width < 2) return PanStep;
    const double span = double(view.spanQ16) / 65536 * Options::PanFull / (samples - 1);
    return int32_t(std::clamp(span / (fine ? view.width - 1 : 10) + .5, 1.0, Options::PanFull / 2.0));
}
bool TraceMove::panLimits(const Options &options, const Model &model, int32_t &low, int32_t &high) {
    const auto &m = model.metadata();
    const auto &shown = model.viewport();
    if (!model.hasFrame() || m.rolling || m.samples < 2 || (options.mode != Mode::Time && options.mode != Mode::SpectrumOverlay))
        return false;
    Options origin = options; origin.panPpb = 0;
    Viewport view; // The window at pan 0: a pan p moves its start by record * p / PanFull (Viewport::calculate).
    if (!view.calculate(m, origin, shown.width, shown.height, shown.phaseQ16)) return false;
    // Half a window past either end of the record, so a press always moves part of it; a zoomed-out
    // window stays inside it (Viewport::calculate clamps it there).
    const double record = double(m.samples - 1) * 65536, reach = options.zoomOut > 1 ? 0 : view.spanQ16 / 2.0;
    const double first = -reach - double(view.startQ16), last = record - double(view.spanQ16) + reach - double(view.startQ16);
    const double scale = Options::PanFull / record;
    low = int32_t(std::clamp(std::ceil(std::min(first, last) * scale), -double(Options::PanFull), 0.0));
    high = int32_t(std::clamp(std::floor(std::max(first, last) * scale), 0.0, double(Options::PanFull)));
    return true;
}
bool TraceMove::pan(Options &options, int horizontal, const Model *model, bool fine) {
    if (!horizontal) return false;
    int32_t low = -Options::PanFull, high = Options::PanFull;
    if (model) panLimits(options, *model, low, high);
    // The timebase or a stored setting may leave the pan outside today's limits.
    const int64_t start = std::clamp(options.panPpb, low, high), step = panStep(model, fine);
    // Issues #127, #129: the steps land on whole steps from pan 0. A pan between them (a limit, DISPLAY's
    // Pan, another time/div or zoom) first goes to the nearest one that way, so returning reaches 0.
    const int64_t below = start / step - (start % step < 0);
    const int64_t offset = start - below * step;
    const int64_t above = below + (offset != 0);
    // Consecutive records can lose a few points to lane alignment, moving the grid by a
    // fraction of a pixel. Keep the same grid index through that rounding.
    const int64_t tolerance = std::max<int64_t>(1, step / 256);
    int64_t grid = horizontal > 0 ? below : above;
    if (offset <= tolerance) grid = below;
    else if (step - offset <= tolerance) grid = above;
    const auto next = int32_t(std::clamp<int64_t>((grid + horizontal) * step, low, high));
    const bool changed = next != options.panPpb;
    options.panPpb = next;
    return changed;
}
TraceMove::Change TraceMove::apply(ScopeRequest &request, Options &options, unsigned channel, bool held, int horizontal, int vertical, bool fine,
                                   const Model *model) {
    Change change;
    const int analog = fine ? AnalogFine : AnalogCoarse, digital = fine ? DigitalFine : DigitalCoarse;
    if (options.mode == Mode::XY) { // CH1 is the X axis and CH2 the Y axis; a lower screen line is a lower voltage.
        if (held) {
            change.view = shiftDigital(options, 0, -horizontal * digital); // As RUN's analog shift: Right moves it right.
            change.view = shiftDigital(options, 1, vertical * digital) || change.view;
        } else {
            change.request = shiftAnalog(request, 0, -horizontal * analog);
            change.request = shiftAnalog(request, 1, vertical * analog) || change.request;
        }
        return change;
    }
    change.view = pan(options, horizontal, model, false); // Arrows: one division a step (S09).
    if (held) change.view = shiftDigital(options, channel, vertical * digital) || change.view;
    else change.request = shiftAnalog(request, channel, vertical * analog);
    return change;
}
void XyMode::enter(ScopeRequest &request, Options &options) {
    request.trigger.mode = scope::TriggerMode::Auto;
    request.timebaseNs = std::min(request.timebaseNs, MaximumTimebaseNs);
    options.persistenceMs = 0;
}
void XyMode::constrain(const Options &options, ScopeRequest &request) {
    if (options.mode == Mode::XY) request.timebaseNs = std::min(request.timebaseNs, MaximumTimebaseNs);
}
bool ChannelDisplay::show(ScopeRequest &request, Options &options, unsigned channel, bool on) {
    if (channel > 1) return false;
    const unsigned other = channel ^ 1;
    auto &axis = options.meter.axis;
    if (on) { // The hidden time axis gives way: shown again if it is this channel, else off.
        if (!request.setChannel(channel, true)) return false;
        if (axis == other && request.setChannel(other, false)) axis = MeterTraceOptions::NoAxis;
        if (axis == channel) axis = MeterTraceOptions::NoAxis;
        return true;
    }
    if (!shown(request, options, other) && !options.meter.shown) return false; // The last channel shown stays on.
    if (request.frontend.enabled[other]) request.setChannel(channel, false);
    else axis = uint8_t(channel);
    if (!shown(request, options, 0) && !shown(request, options, 1)) request.trigger.mode = scope::TriggerMode::Auto;
    return true;
}
void ChannelDisplay::meter(Options &options, bool on) {
    options.meter.shown = on;
    if (!on) options.meter.axis = MeterTraceOptions::NoAxis; // Its time axis is shown again.
}
} // namespace openosc::plot
