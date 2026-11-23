#pragma once
#include <cstdint>
#include "openosc/acquisition/spectrum.hpp"
#include "openosc/ui/scope/meter_scale.hpp"

namespace openosc::plot {
constexpr unsigned MaximumWidth = 320, MaximumHeight = 240, MaximumXYPoints = 640;
constexpr int16_t Missing = -32768;
constexpr uint16_t InfinitePersistence=65535;
enum class Mode : uint8_t { Time, XY, Spectrum, SpectrumOverlay };
enum class Style : uint8_t { Vectors, Points, Envelope };
enum class Grid : uint8_t { Full, Major, Axes, None };
enum class Math : uint8_t { Off, Add, Subtract };
// Samples of a column: Peak (its extremes), Mean (HD: their average), Adaptive (each trace takes
// Mean when its columns hold only noise around a slower signal, else Peak: Model::effectiveMean).
enum class Reduction : uint8_t { Peak, Mean, Adaptive };
enum class Interpolation : uint8_t { Linear, Sinc, Spline };
// What the arrows act on: F1..F4, and Zoom & Pan (issue #96: Up/Down zoom, Left/Right pan; F4 Back).
enum class Focus : uint8_t { Scale, Position, Trigger, Cursor, Zoom };
enum class CursorLine : uint8_t { None, TimeA, TimeB, LevelA, LevelB };
// Density grading (GitHub #85): a channel drawn by how often its recent fresh records crossed each
// pixel (Persistence keeps the levels), in heat colours or one colour's intensity.
enum class Grade : uint8_t { Off, Colors, Mono };
// Grid and trace brightness (GitHub #86): the percent of a colour shown over the plot's background
// (100: the colour itself, 0: the background), in steps of Step. A trace keeps at least LowestTrace:
// Visible hides it. A value between steps (a hand-edited file) moves to the adjacent step.
struct Brightness {
    static constexpr uint8_t Full = 100, Step = 10, LowestGrid = 0, LowestTrace = 10;
    static constexpr unsigned choices(uint8_t lowest) { return (Full - lowest) / Step + 1U; }
    // The choice of a percent; choices(lowest) for one between steps.
    static constexpr unsigned choice(uint8_t percent, uint8_t lowest) {
        return percent >= lowest && percent <= Full && percent % Step == 0 ? unsigned(percent - lowest) / Step : choices(lowest);
    }
    static constexpr uint8_t percent(unsigned choice, uint8_t lowest) { return uint8_t(lowest + choice * Step); }
    static constexpr uint8_t step(uint8_t percent, int direction, uint8_t lowest) {
        const int next = direction > 0 ? (percent / Step + 1) * Step : ((percent + Step - 1) / Step - 1) * Step;
        return uint8_t(next < lowest ? lowest : next > Full ? Full : next);
    }
};
struct ChannelOptions {
    static constexpr uint8_t InheritColor = 0xff;
    int16_t positionPermille = 500;
    uint16_t gainPercent = 100;
    uint8_t width = 1, opacity = 255;
    bool visible = true, invert = false;
    // The held view's mV/div at the probe tip, or 0 for the recorded scale. This changes only
    // projection; the held samples, calibration and acquisition metadata retain their values.
    float heldMillivoltsAtTip = 0;
    Grade grade = Grade::Off;
    uint8_t gradeColor = InheritColor; // Mono's colour: the channel's own, else a ColorCatalog swatch.
    // Its trace's shade (Brightness, GitHub #86): trace, Peak Hold, Persistence, Glow, X-Y and
    // density alike; opacity stays the older see-through alpha of the settings file.
    uint8_t brightness = Brightness::Full;
    bool graded() const { return grade != Grade::Off; }
};
struct CursorOptions {
    uint16_t xA = 250, xB = 750, yA = 250, yB = 750;
    uint8_t channel = 0;
    bool time = false, voltage = false;
};
// Cursor positions are thousandths of the plot, so they survive a change of its size.
inline int cursorPixel(unsigned permille, int span) { return int((int64_t(permille) * span + 500) / 1000); }
// The frequency counter overlay: its source (0 off, 1 CH1, 2 CH2), readings averaged, correction.
struct CounterOptions {
    uint8_t source = 0, average = 1;
    int16_t ppm = 0;
    bool valid() const { return source <= 2 && (average == 1 || average == 3 || average == 10) && ppm >= -1000 && ppm <= 1000; }
};
// The Scope's DMM trace (DataAccess::meterReadings, drawn in Roll): shown or not, where its zero lies
// and each quantity's units/div (MeterScale steps, in MeterScale's order).
struct MeterTraceOptions {
    // Its zero within the plot, as a channel's: four divisions either way of the middle line.
    static constexpr int8_t PositionSteps = 20, PositionLimit = 4 * PositionSteps; // Per division.
    bool shown = false;
    int8_t position = 0; // Its zero above the plot's middle line, in 1/PositionSteps of a division.
    int8_t scales[MeterScale::Quantities] = {MeterScale::Spans[0].initial, MeterScale::Spans[1].initial,
                                             MeterScale::Spans[2].initial, MeterScale::Spans[3].initial,
                                             MeterScale::Spans[4].initial, MeterScale::Spans[5].initial};
    bool valid() const;
};
struct Options {
    Mode mode = Mode::Time;
    Style style = Style::Vectors;
    Grid grid = Grid::Full;
    Math math = Math::Off;
    Reduction reduction = Reduction::Peak;
    Interpolation interpolation = Interpolation::Linear;
    ChannelOptions channels[2];
    CursorOptions cursors;
    scope::SpectrumOptions spectrum;
    CounterOptions counter;
    uint16_t zoom = 1, persistenceMs = 0;
    int32_t panPpb = 0; // Parts per billion of the record (PanFull: all of it).
    static constexpr int32_t PanFull = 1000000000;
    // zoomOut views more than one screen of a longer record. Zoom & Pan keeps one of zoom/zoomOut at 1 (ViewZoom).
    uint8_t averaging = 1, zoomOut = 1;
    uint8_t gridBrightness = Brightness::Full; // Brightness, GitHub #86.
    static constexpr uint16_t MaximumZoom = 256;
    static constexpr uint8_t MaximumZoomOut = 64;
    bool lockTrigger = true, showTrigger = true, showZero = true, glow = false, peakHold = false,
         showScale = true;
    bool centerHeldTrigger = false; // View-only override of a held record's original pretrigger.
    MeterTraceOptions meter; // Bytes, with the flags before heldTimebaseNs: in what was its padding.
    // The time/div a held record is shown at: the next capture's, chosen in HOLD (issue #7), so the
    // held view shows what RUN will use. 0: the record's own. View only, never the record's metadata.
    uint64_t heldTimebaseNs = 0;
    // A density plane's history (GitHub #85): the persistence's, or DensityHistoryMs with none.
    static constexpr uint16_t DensityHistoryMs = 1000;
    uint16_t densityHistoryMs() const { return persistenceMs ? persistenceMs : DensityHistoryMs; }
    bool graded() const { return (mode == Mode::Time || mode == Mode::SpectrumOverlay) && (channels[0].graded() || channels[1].graded()); }
    bool valid() const;
    bool sameTransform(const Options &other) const;
    // What HOLD may change to explore one record; a new capture starts from `saved` again.
    void restoreHeldView(const Options &saved);
};
struct Point {
    int16_t x = Missing, y = Missing;
};
// First, two chronological extrema, last. Missing never becomes an on-screen zero.
struct Column {
    int16_t first = Missing, a = Missing, b = Missing, last = Missing;
};
struct Trace {
    Column columns[MaximumWidth];
    int16_t zero = 0;
    bool enabled = false, clippedTop = false, clippedBottom = false, adcClipped = false;
    bool mean = false; // Its columns show the average of their samples (HD or frame averaging), not their extremes.
};
} // namespace openosc::plot
