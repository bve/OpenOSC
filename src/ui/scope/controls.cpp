#include "openosc/ui/scope/controls.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include <algorithm>

namespace openosc::plot {
namespace {
template <class T> void step(T &value, int direction, int amount, int minimum, int maximum) {
    value = T(std::clamp<int>(int(value) + direction * amount, minimum, maximum));
}
template <class T, size_t N> void cycle(T &value, int direction, const T (&choices)[N]) {
    unsigned index = 0;
    while (index < N && choices[index] != value)
        ++index;
    if (index == N)
        index = 0;
    value = choices[(index + N + direction) % N];
}
} // namespace
void Controls::adjust(unsigned row, int direction, bool fine) {
    if (!direction)
        return;
    direction = direction > 0 ? 1 : -1;
    auto &c = options.channels[channel];
    auto &cursor = options.cursors;
    static constexpr uint16_t persist[] = {0,250,500,1000,5000,10000,InfinitePersistence}, zooms[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
    static constexpr uint8_t averages[] = {1, 4, 16, 64}, counterAverages[] = {1, 3, 10};
    switch (row) {
    case Section:
        group = Group((unsigned(group)+unsigned(Group::Count)+direction)%unsigned(Group::Count));
        break;
    case Channel:
        channel ^= 1;
        cursor.channel = channel;
        break;
    case View:
        options.mode = Mode((unsigned(options.mode) + 4 + direction) % 4);
        break;
    case FftSource:
        if (enabled[options.spectrum.channel^1]) options.spectrum.channel^=1;
        if (options.mode==Mode::Spectrum) { channel=options.spectrum.channel; cursor.channel=channel; }
        break;
    case FftWindow: options.spectrum.window=scope::SpectrumWindow((unsigned(options.spectrum.window)+4+direction)%4); break;
    case FftScale: options.spectrum.scale=scope::SpectrumScale((unsigned(options.spectrum.scale)+1)%2); break;
    case FftDc: options.spectrum.removeDc=!options.spectrum.removeDc; break;
    case CounterSource: options.counter.source=uint8_t((options.counter.source+3+direction)%3); break;
    case CounterAverage: cycle(options.counter.average, direction, counterAverages); break;
    case CounterPpm: step(options.counter.ppm, direction, fine ? 1 : 10, -1000, 1000); break;
    case TraceStyle:
        options.style = Style((unsigned(options.style) + 3 + direction) % 3);
        break;
    case SampleReduction:
        options.reduction=Reduction((unsigned(options.reduction)+3+direction)%3);
        break;
    case TraceInterpolation:
        options.interpolation=Interpolation((unsigned(options.interpolation)+3+direction)%3);
        break;
    case GridStyle:
        options.grid = Grid((unsigned(options.grid) + 4 + direction) % 4);
        break;
    case Persistence:
        cycle(options.persistenceMs, direction, persist);
        break;
    case Average:
        cycle(options.averaging, direction, averages);
        break;
    case PeakHold:
        options.peakHold = !options.peakHold;
        break;
    case Glow:
        options.glow = !options.glow;
        break;
    case Zoom:
        cycle(options.zoom, direction, zooms);
        break;
    case Pan:
        step(options.panPpb, direction, TraceMove::panStep(model, fine), -Options::PanFull, Options::PanFull);
        break;
    case Visible:
        c.visible = !c.visible;
        break;
    case Invert:
        c.invert = !c.invert;
        break;
    case Gain:
        step(c.gainPercent, direction, 25, 1, 6400);
        break;
    case Position:
        step(c.positionPermille, direction, 25, -1000, 2000);
        break;
    case TimeCursors:
        cursor.time = !cursor.time;
        break;
    case VoltageCursors:
        cursor.voltage = !cursor.voltage;
        break;
    case CursorAX:
        step(cursor.xA, direction, fine ? 1 : 25, 0, 1000);
        break;
    case CursorBX:
        step(cursor.xB, direction, fine ? 1 : 25, 0, 1000);
        break;
    case CursorAY:
        step(cursor.yA, direction, fine ? 1 : 25, 0, 1000);
        break;
    case CursorBY:
        step(cursor.yB, direction, fine ? 1 : 25, 0, 1000);
        break;
    case MathTrace:
        options.math = Math((unsigned(options.math) + 3 + direction) % 3);
        break;
    default:
        break;
    }
}
} // namespace openosc::plot
