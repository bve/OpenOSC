#include "openosc/ui/scope/controls.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::plot {
namespace {
using Kind = ui::PropertyKind;
constexpr uint16_t Persistences[] = {0, 250, 500, 1000, 5000, 10000, InfinitePersistence}, Zooms[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};
constexpr uint8_t Averages[] = {1, 4, 16, 64};
template <class T, size_t N> unsigned indexOf(T value, const T (&choices)[N]) {
    for (unsigned i = 0; i < N; ++i) if (choices[i] == value) return i;
    return N;
}
}
Kind Controls::kind(unsigned row) const {
    switch (row) {
    case PeakHold: case Glow: case Visible: case Invert: case TimeCursors: case VoltageCursors: case FftDc: return Kind::Toggle;
    case Pan: case Gain: case Position: case CursorAX: case CursorBX: case CursorAY: case CursorBY: case CounterPpm: return Kind::Number;
    case CenterTrigger: case Reference: case Clear: return Kind::Action;
    default: return Kind::Choice;
    }
}
void Controls::name(unsigned row, char *text, size_t capacity) const {
    const bool spectrum = options.mode == Mode::Spectrum;
    switch (row) {
    case Section: std::snprintf(text, capacity, "Section"); break;
    case Channel: std::snprintf(text, capacity, "Channel"); break;
    case View: std::snprintf(text, capacity, "View"); break;
    case TraceStyle: std::snprintf(text, capacity, "Trace"); break;
    case GridStyle: std::snprintf(text, capacity, "Grid"); break;
    case Persistence: std::snprintf(text, capacity, "Persistence"); break;
    case Average: std::snprintf(text, capacity, "Average"); break;
    case PeakHold: std::snprintf(text, capacity, "Peak hold"); break;
    case Glow: std::snprintf(text, capacity, "Glow"); break;
    case Zoom: std::snprintf(text, capacity, "Zoom"); break;
    case Pan: std::snprintf(text, capacity, "Horizontal position"); break;
    case CenterTrigger: std::snprintf(text, capacity, "Center trigger"); break;
    case Visible: std::snprintf(text, capacity, "CH%u visible", channel + 1); break;
    case Invert: std::snprintf(text, capacity, "CH%u invert", channel + 1); break;
    case Gain: std::snprintf(text, capacity, "CH%u digital gain", channel + 1); break;
    case Position: std::snprintf(text, capacity, "CH%u digital offset", channel + 1); break;
    case TimeCursors: std::snprintf(text, capacity, "%s cursors", spectrum ? "Frequency" : "Time"); break;
    case VoltageCursors: std::snprintf(text, capacity, "%s cursors", spectrum ? "Level" : "Voltage"); break;
    case CursorAX: std::snprintf(text, capacity, "Cursor A %s", spectrum ? "freq" : "time"); break;
    case CursorBX: std::snprintf(text, capacity, "Cursor B %s", spectrum ? "freq" : "time"); break;
    case CursorAY: std::snprintf(text, capacity, "Cursor a level"); break;
    case CursorBY: std::snprintf(text, capacity, "Cursor b level"); break;
    case MathTrace: std::snprintf(text, capacity, "MATH"); break;
    case Reference: std::snprintf(text, capacity, "Capture CH%u reference", channel + 1); break;
    case Clear: std::snprintf(text, capacity, "Clear history and reference"); break;
    case SampleReduction: std::snprintf(text, capacity, "Samples"); break;
    case TraceInterpolation: std::snprintf(text, capacity, "Interpolation"); break;
    case FftSource: std::snprintf(text, capacity, "FFT source"); break;
    case FftWindow: std::snprintf(text, capacity, "FFT window"); break;
    case FftScale: std::snprintf(text, capacity, "FFT scale"); break;
    case FftDc: std::snprintf(text, capacity, "FFT remove DC"); break;
    case CounterSource: std::snprintf(text, capacity, "Counter"); break;
    case CounterAverage: std::snprintf(text, capacity, "Readings averaged"); break;
    case CounterPpm: std::snprintf(text, capacity, "Clock correction"); break;
    default: if (capacity) text[0] = 0; break;
    }
}
unsigned Controls::choices(unsigned row) const {
    switch (row) {
    case Section: return unsigned(Group::Count);
    case Channel: case FftSource: case FftScale: return 2;
    case View: case GridStyle: case Average: case FftWindow: return 4;
    case TraceStyle: case MathTrace: case TraceInterpolation: case SampleReduction: case CounterSource: case CounterAverage: return 3;
    case Persistence: return sizeof Persistences / sizeof *Persistences;
    case Zoom: return sizeof Zooms / sizeof *Zooms;
    default: return kind(row) == Kind::Toggle ? 2 : 0;
    }
}
unsigned Controls::choice(unsigned row) const {
    const auto &o = options;
    const auto &c = o.channels[channel];
    switch (row) {
    case Section: return unsigned(group);
    case Channel: return channel;
    case View: return unsigned(o.mode);
    case TraceStyle: return unsigned(o.style);
    case GridStyle: return unsigned(o.grid);
    case Persistence: return indexOf(o.persistenceMs, Persistences);
    case Average: return indexOf(o.averaging, Averages);
    case Zoom: return indexOf(o.zoom, Zooms);
    case MathTrace: return unsigned(o.math);
    case SampleReduction: return unsigned(o.reduction);
    case TraceInterpolation: return unsigned(o.interpolation);
    case FftSource: return o.spectrum.channel;
    case FftWindow: return unsigned(o.spectrum.window);
    case FftScale: return unsigned(o.spectrum.scale);
    case PeakHold: return o.peakHold;
    case Glow: return o.glow;
    case Visible: return c.visible;
    case Invert: return c.invert;
    case TimeCursors: return o.cursors.time;
    case VoltageCursors: return o.cursors.voltage;
    case FftDc: return o.spectrum.removeDc;
    case CounterSource: return o.counter.source;
    case CounterAverage: return o.counter.average == 1 ? 0 : o.counter.average == 3 ? 1 : 2;
    default: return 0;
    }
}
void Controls::choiceName(unsigned row, unsigned i, char *text, size_t capacity) const {
    static constexpr const char *views[] = {"Y-T", "X-Y", "FFT", "Y-T+FFT"}, *styles[] = {"Vectors", "Points", "Envelope"},
        *grids[] = {"Full", "Major", "Axes", "None"}, *maths[] = {"Off", "CH1+CH2", "CH1-CH2"}, *reductions[] = {"Peak", "HD mean", "Auto (HD/Peak)"},
        *interpolations[] = {"Linear", "Sinc", "Spline"}, *windows[] = {"Rectangular", "Hann", "Hamming", "Blackman"},
        *scales[] = {"Linear", "dBFS"}, *counters[] = {"Off", "CH1", "CH2"}, *averaged[] = {"1", "3", "10"};
    const auto pick = [&](const char *const *names, unsigned n) { std::snprintf(text, capacity, "%s", i < n ? names[i] : ""); };
    switch (row) {
    case Section: std::snprintf(text, capacity, "%s", groupName(i)); break;
    case Channel: case FftSource: std::snprintf(text, capacity, "CH%u", i + 1); break;
    case View: pick(views, 4); break;
    case TraceStyle: pick(styles, 3); break;
    case GridStyle: pick(grids, 4); break;
    case MathTrace: pick(maths, 3); break;
    case SampleReduction: pick(reductions, 3); break;
    case TraceInterpolation: pick(interpolations, 3); break;
    case FftWindow: pick(windows, 4); break;
    case FftScale: pick(scales, 2); break;
    case CounterSource: pick(counters, 3); break;
    case CounterAverage: pick(averaged, 3); break;
    case Persistence:
        if (i < choices(row) && Persistences[i] == InfinitePersistence) std::snprintf(text, capacity, "Infinite");
        // A graded channel (GitHub #85) keeps its density for the persistence, or this with none.
        else if (i == 0 && options.graded()) std::snprintf(text, capacity, "Off (density %u s)", Options::DensityHistoryMs / 1000U);
        else std::snprintf(text, capacity, "%u ms", i < choices(row) ? Persistences[i] : options.persistenceMs);
        break;
    case Average: std::snprintf(text, capacity, "%u", i < choices(row) ? Averages[i] : options.averaging); break;
    case Zoom: std::snprintf(text, capacity, "%ux", i < choices(row) ? Zooms[i] : options.zoom); break;
    default:
        if (kind(row) == Kind::Toggle) std::snprintf(text, capacity, "%s", i ? "On" : "Off");
        else if (capacity) text[0] = 0;
        break;
    }
}
void Controls::value(unsigned row, char *text, size_t capacity) const {
    const auto &o = options;
    const auto &cursor = o.cursors;
    switch (row) {
    case Pan:
        std::snprintf(text, capacity, "%ld/1000", long(number(row)));
        break;
    case Gain: std::snprintf(text, capacity, "%u%%", o.channels[channel].gainPercent); break;
    case Position: std::snprintf(text, capacity, "%d/1000", o.channels[channel].positionPermille - 500); break;
    case CursorAX: std::snprintf(text, capacity, "%u/1000", cursor.xA); break;
    case CursorBX: std::snprintf(text, capacity, "%u/1000", cursor.xB); break;
    case CursorAY: std::snprintf(text, capacity, "%u/1000", cursor.yA); break;
    case CursorBY: std::snprintf(text, capacity, "%u/1000", cursor.yB); break;
    case CounterPpm: std::snprintf(text, capacity, "%+d ppm", o.counter.ppm); break;
    default:
        if (kind(row) == Kind::Choice || kind(row) == Kind::Toggle) choiceName(row, choice(row), text, capacity);
        else if (capacity) text[0] = 0;
        break;
    }
}
int32_t Controls::number(unsigned row) const {
    const auto &cursor = options.cursors;
    switch (row) {
    case Pan: return (options.panPpb + (options.panPpb < 0 ? -PpbPerPermille / 2 : PpbPerPermille / 2)) / PpbPerPermille;
    case Gain: return options.channels[channel].gainPercent;
    case Position: return options.channels[channel].positionPermille;
    case CursorAX: return cursor.xA;
    case CursorBX: return cursor.xB;
    case CursorAY: return cursor.yA;
    case CursorBY: return cursor.yB;
    case CounterPpm: return options.counter.ppm;
    default: return 0;
    }
}
void Controls::setNumber(unsigned row, int32_t value) {
    auto &cursor = options.cursors;
    switch (row) {
    case Pan: options.panPpb = std::clamp(value, int32_t(-1000), int32_t(1000)) * PpbPerPermille; break;
    case Gain: options.channels[channel].gainPercent = uint16_t(value); break;
    case Position: options.channels[channel].positionPermille = int16_t(value); break;
    case CursorAX: cursor.xA = uint16_t(value); break;
    case CursorBX: cursor.xB = uint16_t(value); break;
    case CursorAY: cursor.yA = uint16_t(value); break;
    case CursorBY: cursor.yB = uint16_t(value); break;
    case CounterPpm: options.counter.ppm = int16_t(std::clamp<int32_t>(value, -1000, 1000)); break;
    default: break;
    }
}
} // namespace openosc::plot
