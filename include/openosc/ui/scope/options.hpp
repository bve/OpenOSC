#pragma once
#include <cstdint>
#include "openosc/acquisition/spectrum.hpp"

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
enum class Focus : uint8_t { Scale, Position, Trigger, Cursor }; // What the arrows act on: F1..F4.
enum class CursorLine : uint8_t { None, TimeA, TimeB, LevelA, LevelB };
// Density grading (GitHub #85): a channel drawn by how often its recent fresh records crossed each
// pixel (Persistence keeps the levels), in heat colours or one colour's intensity.
enum class Grade : uint8_t { Off, Colors, Mono };
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
    uint8_t averaging = 1, zoomOut = 1; // zoomOut views more than one screen of a longer record.
    bool lockTrigger = true, showTrigger = true, showZero = true, glow = false, peakHold = false,
         showScale = true;
    // The time/div a held record is shown at: the next capture's, chosen in HOLD (issue #7), so the
    // held view shows what RUN will use. 0: the record's own. View only, never the record's metadata.
    uint64_t heldTimebaseNs = 0;
    bool centerHeldTrigger = false; // View-only override of a held record's original pretrigger.
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
