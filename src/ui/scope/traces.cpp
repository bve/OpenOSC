#include "openosc/ui/scope/painter.hpp"
#include "openosc/measurement/dmm/reading_trace.hpp"
#include "trace_geometry.hpp"
#include <algorithm>
#include <cmath>

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
    meterTrace();
    if (o.math != Math::Off) {
        trace(s.traces[2], palette_.math, 220, 1, o.style);
        // The operation keeps its input order. Show each input's on-screen number.
        const char label[] = {'M', ':', ' ', char('0' + palette_.view.number(0)),
                              o.math == Math::Add ? '+' : '-', char('0' + palette_.view.number(1)), '\0'};
        text(s.traces[2].enabled ? label : "M: N/A", 8, 3, palette_.math, 80);
    }
    if (model_.hasReference())
        text("REF", model_.viewport().width - 28, 3, palette_.reference, 28);
    if (o.averaging > 1)
        text("AVG", 8, model_.viewport().height - 14, palette_.readout, 30);
}
// The DMM trace in Roll (Y-T, Y-T+FFT): each reading at its time on the snapshot's axis (the oldest point at
// metadata.timestampUs, one point per 1 / samplesPerSecond), joined to the next; a reading without a
// value breaks the line. A capture of another kind started by RenderIdle while a line is drawn gives
// the readings up: valid() and size() are asked again before each one.
void Painter::meterTrace() {
    const auto *readings = meter_;
    const auto &o = model_.options();
    const auto &m = model_.metadata();
    const auto &view = model_.viewport();
    const double rate = m.samplesPerSecond.value();
    if (!readings || !o.meter.shown || !m.rolling || !readings->valid() ||
        !MeterScale::scaled(readings->quantity()) || rate <= 0 || view.width < 2 || view.height < 2)
        return;
    const double perDivision = MeterScale::perDivision(o.meter.scales[MeterScale::index(readings->quantity())]);
    const double division = double(view.height - 1) / 8;
    const double zero = double(view.height - 1) / 2 - double(o.meter.position) / MeterTraceOptions::PositionSteps * division;
    const uint32_t originMs = uint32_t(m.timestampUs / 1000);
    const double originUs = double(m.timestampUs % 1000);
    Path path(layer_, area_, palette_.reference, 255, 1, model_.statistics());
    int leftX = 0, leftY = 0;
    bool left = false; // The last reading before the window: the line enters from it.
    for (size_t i = 0; readings->valid() && i < readings->size(); ++i) {
        const auto reading = (*readings)[i];
        if (!std::isfinite(reading.value)) { path.gap(); left = false; continue; }
        const double us = double(int32_t(reading.ms - originMs)) * 1000 - originUs;
        const double sample = std::clamp(us * rate / 1e6, -1e9, 1e9); // Far off the plot either way: positionX stays in int64.
        const int x = view.positionX(int64_t(sample * 65536));
        const int y = int(std::clamp(zero - reading.value / perDivision * division, -4096.0, 4096.0));
        if (x < 0) { leftX = x; leftY = y; left = true; continue; }
        if (left) { path.vertex(leftX, leftY); left = false; }
        path.vertex(x, y);
    }
}
} // namespace openosc::plot
