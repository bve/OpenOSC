#include "openosc/ui/scope/view_zoom.hpp"
#include "openosc/ui/scope/model.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <numeric>

namespace openosc::plot {
bool ViewZoom::available(const Options &options, const Model &model) {
    const auto &m = model.metadata();
    return model.hasFrame() && !m.rolling && m.samples >= 2 && (options.mode == Mode::Time || options.mode == Mode::SpectrumOverlay);
}
ViewZoom::Step ViewZoom::step(Options &options, int direction, const Model &model) {
    if (!available(options, model)) return Step::Unavailable;
    if (!direction) return Step::Limit;
    Options next = options;
    if (direction > 0) {
        if (next.zoomOut > 1) next.zoomOut /= 2;
        else if (next.zoom < Options::MaximumZoom) next.zoom = std::min<unsigned>(Options::MaximumZoom, next.zoom * 2);
        else return Step::Limit;
    } else {
        if (next.zoom > 1) next.zoom /= 2;
        else if (next.zoomOut < Options::MaximumZoomOut) next.zoomOut = std::min<unsigned>(Options::MaximumZoomOut, next.zoomOut * 2);
        else return Step::Limit;
    }
    const auto &m = model.metadata();
    const auto &shown = model.viewport();
    Viewport before, after;
    if (!before.calculate(m, options, shown.width, shown.height, shown.phaseQ16) ||
        !after.calculate(m, next, shown.width, shown.height, shown.phaseQ16))
        return Step::Unavailable;
    if (direction > 0 && after.spanQ16 < (uint64_t(MinimumIntervals) << 16)) return Step::Limit;
    const uint64_t record = uint64_t(m.samples - 1) << 16;
    if (direction < 0 && (before.spanQ16 >= record || !fits(m, next) || after.spanQ16 <= before.spanQ16))
        return Step::Limit;
    // The pan as a step of 0 would leave it: clamped into the limits of the new window.
    int32_t low = -Options::PanFull, high = Options::PanFull;
    if (TraceMove::panLimits(next, model, low, high)) next.panPpb = std::clamp(next.panPpb, low, high);
    options = next;
    return Step::Changed;
}
bool ViewZoom::reset(Options &options) {
    const bool changed = !home(options);
    options.zoom = options.zoomOut = 1;
    options.panPpb = 0;
    return changed;
}
bool ViewZoom::fits(const scope::FrameMetadata &m, const Options &options) {
    if (options.zoomOut <= 1) return true; // Viewport bounds only a zoomed-out span by the record.
    const uint64_t requested = Viewport::requestedSpanQ16(m, options);
    if (!requested) return false;
    const uint64_t record = uint64_t(m.samples - 1) << 16;
    // The ten divisions' span is rounded to 1/65536 of an interval before zoomOut multiplies it.
    return requested <= record + options.zoomOut;
}
void ViewZoom::shownFactor(const Options &options, const Model &model, char *output, size_t capacity) {
    if (model.hasFrame() && !fits(model.metadata(), options)) std::snprintf(output, capacity, "all");
    else factor(options, output, capacity);
}
void ViewZoom::marker(const Options &options, const Model &model, char *output, size_t capacity) {
    if (!capacity) return;
    *output = 0;
    if ((options.mode != Mode::Time && options.mode != Mode::SpectrumOverlay) || home(options)) return;
    // The whole record shown has no room to pan in. Written in place: no buffer of its own (stack).
    const bool zoomed = options.zoom != 1 || options.zoomOut != 1;
    const bool panned = options.panPpb && (!model.hasFrame() || fits(model.metadata(), options));
    size_t used = 0;
    if (zoomed && capacity > 5) {
        std::snprintf(output, capacity, "ZOOM ");
        shownFactor(options, model, output + 5, capacity - 5);
        used = std::strlen(output);
    }
    if (panned && used < capacity) std::snprintf(output + used, capacity - used, "%sPAN", used ? " " : "");
}
void ViewZoom::factor(const Options &options, char *output, size_t capacity) {
    const unsigned divisor = std::gcd(unsigned(options.zoom), std::max(1U, unsigned(options.zoomOut)));
    const unsigned numerator = options.zoom / divisor;
    const unsigned denominator = options.zoomOut / divisor;
    if (denominator > 1) std::snprintf(output, capacity, "%u/%ux", numerator, denominator);
    else std::snprintf(output, capacity, "%ux", numerator);
}
} // namespace openosc::plot
