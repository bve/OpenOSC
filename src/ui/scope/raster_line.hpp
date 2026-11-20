#pragma once
#include <cstdint>
#include <cstdlib>

namespace openosc::plot {
// Clip projected coordinates before Bresenham, keeping even a heavily
// over-range trace bounded by the plot.
inline bool clipLine(int &x1, int &y1, int &x2, int &y2, int right, int bottom) {
    const auto code = [&](int x, int y) { return (x<0 ? 1 : x>right ? 2 : 0) | (y<0 ? 4 : y>bottom ? 8 : 0); };
    for (;;) {
        const int a=code(x1,y1), b=code(x2,y2);
        if (!(a|b)) return true;
        if (a&b) return false;
        const int out=a?a:b;
        int x,y;
        if (out&12) {
            y=out&4 ? 0 : bottom;
            x=x1+int64_t(x2-x1)*(y-y1)/(y2-y1);
        } else {
            x=out&1 ? 0 : right;
            y=y1+int64_t(y2-y1)*(x-x1)/(x2-x1);
        }
        if (a) { x1=x; y1=y; } else { x2=x; y2=y; }
    }
}

template <class Mark>
inline void rasterLine(int x1, int y1, int x2, int y2, Mark mark) {
    const int dx=std::abs(x2-x1), dy=-std::abs(y2-y1), sx=x1<x2 ? 1 : -1, sy=y1<y2 ? 1 : -1;
    int error=dx+dy;
    for (;;) {
        mark(x1,y1);
        if (x1==x2 && y1==y2) break;
        const int twice=2*error;
        if (twice>=dy) { error+=dy; x1+=sx; }
        if (twice<=dx) { error+=dx; y1+=sy; }
    }
}
} // namespace openosc::plot
