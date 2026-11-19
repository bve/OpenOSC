#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/ui/scope/model.hpp"
#include <algorithm>

namespace openosc::plot {
namespace {
constexpr int AnalogFine = 1, AnalogCoarse = 4, DigitalFine = 7, DigitalCoarse = 25, PanStep = 25;
// One division (coarse) or one pixel (fine) of the shown window, in permille of the record; at least 1.
int panStep(const Model *model, bool fine) {
    if (!model) return PanStep;
    const auto &view = model->viewport();
    const uint32_t samples = model->metadata().samples;
    if (samples < 2 || !view.spanQ16 || view.width < 2) return PanStep;
    const double span = double(view.spanQ16) / 65536 * 1000 / (samples - 1);
    return std::clamp(int(span / (fine ? view.width - 1 : 10) + .5), 1, 500);
}
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
bool TraceMove::pan(Options &options, int horizontal, const Model *model, bool fine) {
    const auto next = int16_t(std::clamp<int>(options.pan + horizontal * panStep(model, fine), -1000, 1000));
    const bool changed = next != options.pan;
    options.pan = next;
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
    change.view = pan(options, horizontal, model, fine);
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
} // namespace openosc::plot
