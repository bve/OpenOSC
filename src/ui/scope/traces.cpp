#include "openosc/ui/scope/painter.hpp"
#include <algorithm>

namespace openosc::plot {
void Painter::trace(const Trace &trace, uint32_t color, uint8_t opacity, unsigned width, Style style,
                    bool center) {
    if (!trace.enabled || !opacity)
        return;
    Path path(layer_, area_, color, opacity, width, model_.statistics());
    Path extrema(layer_, area_, color, opacity, width, model_.statistics());
    int previous = Missing;
    for (unsigned x = 0; x < model_.viewport().width; ++x) {
        const auto &p = trace.columns[x];
        if (p.first == Missing) {
            path.gap();
            previous = Missing;
            continue;
        }
        if (style == Style::Points) {
            path.vertex(x, p.last);
            path.vertex(x, p.last + 1);
            path.gap();
        } else if (center)
            path.vertex(x, (int(p.a) + p.b) / 2);
        else if (style == Style::Envelope) {
            path.vertex(x, std::min(p.a, p.b));
            path.vertex(x, std::max(p.a, p.b) + 1);
            path.gap();
        } else {
            path.vertex(x, p.last);
            const int before = previous == Missing ? p.first : previous;
            if (std::min(p.a, p.b) < std::min(before, int(p.last)) ||
                std::max(p.a, p.b) > std::max(before, int(p.last))) {
                extrema.vertex(x, std::min(p.a, p.b));
                extrema.vertex(x, std::max(p.a, p.b) + 1);
                extrema.gap();
            }
            previous = p.last;
        }
    }
}
void Painter::xy(const Point *points, unsigned count, uint32_t color, uint8_t opacity, unsigned width,
                 Style style) {
    if (!opacity)
        return;
    Path path(layer_, area_, color, opacity, width, model_.statistics());
    for (unsigned i = 0; i < count; ++i) {
        const auto p = points[i];
        if (p.x == Missing || p.y == Missing) {
            path.gap();
            continue;
        }
        path.vertex(p.x, p.y);
        if (style == Style::Points) {
            path.vertex(p.x, p.y + 1);
            path.gap();
        }
    }
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
        if (o.glow)
            xy(s.xy, model_.xyCount(), palette_.channels[0], o.channels[0].opacity / 4, o.channels[0].width + 4,
               o.style);
        xy(s.xy, model_.xyCount(), palette_.channels[0], o.channels[0].opacity, o.channels[0].width, o.style);
        return;
    }
    if (model_.hasReference())
        trace(s.reference, palette_.reference, 160, 1, Style::Vectors, true);
    for (unsigned c = 0; c < 2; ++c) {
        const auto &channel = o.channels[c];
        if (o.peakHold)
            trace(s.peaks[c], palette_.channels[c], 65U * channel.opacity / 255, 1, Style::Envelope);
        if (o.glow)
            trace(s.traces[c], palette_.channels[c], channel.opacity / 4, channel.width + 4, o.style);
        trace(s.traces[c], palette_.channels[c], channel.opacity, channel.width, o.style);
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
