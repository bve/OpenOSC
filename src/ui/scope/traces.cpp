#include "openosc/ui/scope/painter.hpp"
#include "openosc/measurement/dmm/reading_trace.hpp"
#include "openosc/ui/scope/meter_readouts.hpp"
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
// The DMM trace (Y-T, Y-T+FFT) at every timebase: each reading at its time on the record's axis
// (MeterAxis), joined to the next; a reading without a value breaks the line, the one before the
// window enters it at the left edge. The line spans the record only, as its channels do: from its
// first point (Roll's start of record) a reading before it shows as its level alone, held up to the
// next reading and never joined to it; to its last point the newest reading by then is held level: a
// window shorter than the readings' interval shows the value of its time as a level. Readings after
// the record are not of it (they came while it was shown).
void Painter::meterTrace() {
    const auto *readings = meter_;
    const auto &o = model_.options();
    const auto &view = model_.viewport();
    const MeterAxis axis(model_.metadata());
    if (!readings || !o.meter.shown || !MeterScale::scaled(readings->quantity()) || !axis.valid() || !view.spanQ16 ||
        view.width < 2 || view.height < 2)
        return;
    const double perDivision = MeterScale::perDivision(o.meter.scales[MeterScale::index(readings->quantity())]);
    const double division = double(view.height - 1) / 8, zero = meterZero();
    const double start = double(view.startQ16) / 65536, columns = double(view.width - 1) * 65536 / double(view.spanQ16);
    const auto pixel = [](double v) { return int(std::lround(std::clamp(v, -4096.0, 4096.0))); };
    Path path(layer_, area_, palette_.meter, 255, 1, model_.statistics());
    const double left = std::max(0.0, -start * columns); // The record's first point, or the window's edge.
    // The line above or below the plot within the window: the edge marks (markers), as a channel's.
    const double right = double(view.width - 1), bottom = double(view.height - 1);
    bool run = false; // A vertex since the last gap, at (px, py).
    double px = 0, py = 0;
    const auto mark = [&](double y) { meterAbove_ = meterAbove_ || y < 0; meterBelow_ = meterBelow_ || y > bottom; };
    const auto vertex = [&](double x, double y) {
        if (x <= right) mark(y);
        else if (run && px <= right) mark(py + (y - py) * (right - px) / (x - px)); // Where it leaves the window.
        path.vertex(pixel(x), pixel(y));
        run = true; px = x; py = y;
    };
    double x0 = 0, y0 = 0;
    bool line = false; // The last reading taken has a value: the line goes on from (x0, y0).
    bool before = false; // That reading is before the record: its level only, from the record's start.
    for (size_t i = 0; i < readings->size(); ++i) {
        const auto reading = (*readings)[i];
        const double point = axis.point(reading.ms);
        if (point > axis.last) break;
        if (!std::isfinite(reading.value)) { path.gap(); line = run = false; continue; }
        const double x = (point - start) * columns, y = zero - reading.value / perDivision * division;
        if (x >= left) {
            if (line && x0 < left && before) { // Its level up to this reading, then the step to it.
                vertex(left, y0);
                vertex(x, y0);
            } else if (line && x0 < left) vertex(left, y0 + (y - y0) * (left - x0) / (x - x0)); // Where it crosses the edge.
            vertex(x, y);
        }
        x0 = x; y0 = y; line = true; before = point < 0;
    }
    const double end = (axis.last - start) * columns;
    if (!line || end < left) return;
    if (x0 < left) vertex(left, y0);
    vertex(end, y0);
}
} // namespace openosc::plot
