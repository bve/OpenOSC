#pragma once
#include "openosc/ui/scope/options.hpp"
#include <algorithm>

namespace openosc::plot {
template <class Path>
void traceGeometry(const Trace &trace, unsigned width, Style style, bool center, Path &path, Path &extrema) {
    int previous = Missing;
    for (unsigned x = 0; x < width; ++x) {
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
        } else if (center) {
            path.vertex(x, (int(p.a) + p.b) / 2);
        } else {
            const int lo = std::min(p.a, p.b), hi = std::max(p.a, p.b);
            if (style == Style::Envelope) {
                path.vertex(x, lo);
                path.vertex(x, hi + 1);
                path.gap();
            } else {
                path.vertex(x, p.last);
                const int before = previous == Missing ? p.first : previous;
                if (lo < std::min(before, int(p.last)) || hi > std::max(before, int(p.last))) {
                    extrema.vertex(x, lo);
                    extrema.vertex(x, hi + 1);
                    extrema.gap();
                }
                previous = p.last;
            }
        }
    }
}

template <class Path>
void xyGeometry(const Point *points, unsigned count, Style style, Path &path) {
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
} // namespace openosc::plot
