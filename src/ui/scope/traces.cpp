#include "openosc/ui/scope/painter.hpp"
#include "trace_geometry.hpp"

namespace openosc::plot {
void Painter::trace(const Trace &trace, uint32_t color, uint8_t opacity, unsigned width, Style style,
                    bool center) {
    if (!trace.enabled || !opacity)
        return;
    Path path(layer_, area_, color, opacity, width, model_.statistics());
    Path extrema(layer_, area_, color, opacity, width, model_.statistics());
    traceGeometry(trace, model_.viewport().width, style, center, path, extrema);
}
void Painter::xy(const Point *points, unsigned count, uint32_t color, uint8_t opacity, unsigned width,
                 Style style) {
    if (!opacity)
        return;
    Path path(layer_, area_, color, opacity, width, model_.statistics());
    xyGeometry(points, count, style, path);
}
void Painter::traces() {
    const auto &o = model_.options();
    const auto &s = model_.storage();
    if (o.persistenceMs) persistence();
    if (o.mode == Mode::XY) {
        if (!model_.xyCount()) {
            text("XY NEEDS CH1 + CH2", 8, 18, palette_.readout, 180);
            return;
        }
        if (o.glow) glow(0);
        xy(s.xy, model_.xyCount(), traceColor(0), o.channels[0].opacity, o.channels[0].width, o.style); // CH1's trace.
        return;
    }
    if (model_.hasReference())
        trace(s.reference, palette_.reference, 160, 1, Style::Vectors, true);
    // Each layer back to front: the selected channel last, over the other.
    const unsigned front = model_.frontChannel(), order[2] = {front ^ 1, front};
    if (o.glow)
        for (const unsigned c : order)
            if (!densityShown(c)) glow(c);
    for (const unsigned c : order) {
        const auto &channel = o.channels[c];
        const uint32_t color = traceColor(c);
        if (o.peakHold)
            trace(s.peaks[c], color, 65U * channel.opacity / 255, 1, Style::Envelope);
        // A graded channel is its density, the newest record included; with none yet (cleared by a
        // change of the view, or held before a record), its trace.
        if (densityShown(c)) density(c);
        else trace(s.traces[c], color, channel.opacity, channel.width, o.style);
    }
    if (o.math != Math::Off) {
        trace(s.traces[2], palette_.math, 220, 1, o.style);
        text(s.traces[2].enabled ? o.math == Math::Add ? "M: 1+2" : "M: 1-2" : "M: N/A", 8, 3, palette_.math,
             80);
    }
    if (model_.hasReference())
        text("REF", model_.viewport().width - 28, 3, palette_.reference, 28);
    if (o.averaging > 1)
        text("AVG", 8, model_.viewport().height - 14, palette_.readout, 30);
}
} // namespace openosc::plot
