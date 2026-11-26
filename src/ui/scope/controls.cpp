#include "openosc/ui/scope/controls.hpp"
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
// Digital gain (GitHub #98): ticks 1, 25, 50, ..., 6400 percent. A value between ticks (a stored
// legacy one) moves to the adjacent tick in the direction; 1 stays the lowest (0 has no volts/div).
uint16_t stepGain(uint16_t percent, int direction) {
    constexpr int Tick = 25, Lowest = 1, Highest = 6400;
    const int value = std::clamp<int>(percent, Lowest, Highest);
    const int next = direction > 0 ? (value / Tick + 1) * Tick : ((value - 1) / Tick) * Tick;
    return uint16_t(std::clamp(next, Lowest, Highest));
}
} // namespace
void Controls::adjustPan(int direction, bool fine) {
    const int sign = direction > 0 ? 1 : -1;
    constexpr unsigned RepeatsPerStep = 5, MaxRepeats = 4 * RepeatsPerStep;
    constexpr int Steps[] = {1, 2, 5, 10, 25};
    panEditRepeats_ = fine || sign != panEditSign_ ? 0 : std::min<unsigned>(MaxRepeats, panEditRepeats_ + 1);
    panEditSign_ = sign;
    const int step = std::max(direction * sign, Steps[panEditRepeats_ / RepeatsPerStep]);
    // A plot pan can land between editor ticks. The first step lands on the next tick in that direction.
    const int32_t pan = options.panPpb;
    int32_t ticks = pan / PpbPerPermille;
    if (pan % PpbPerPermille && (pan > 0) != (sign > 0)) ticks -= sign;
    setNumber(Pan, ticks + sign * step);
}
void Controls::adjust(unsigned row, int direction, bool fine) {
    if (!direction)
        return;
    if (row == Pan) { adjustPan(direction, fine); return; }
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
        if (meterChannel()) { // CH1, CH2, DMM.
            cursor.channel = uint8_t((choice(Channel) + 3 + direction) % 3);
            if (cursor.channel < 2) channel = cursor.channel;
            break;
        }
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
    case GridBrightness:
        options.gridBrightness = Brightness::step(options.gridBrightness, direction, Brightness::LowestGrid);
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
    case Zoom: // From a Zoom & Pan factor below 1x: 1x first, then the list.
        if (options.zoomOut > 1) { options.zoomOut = 1; options.zoom = 1; }
        else cycle(options.zoom, direction, zooms);
        break;
    case Visible:
        c.visible = !c.visible;
        break;
    case Invert:
        c.invert = !c.invert;
        break;
    case Gain:
        c.gainPercent = stepGain(c.gainPercent, direction);
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
