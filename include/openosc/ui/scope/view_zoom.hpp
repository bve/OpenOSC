#pragma once
#include "openosc/ui/scope/options.hpp"
#include "openosc/acquisition/frame.hpp"
#include <cstddef>

namespace openosc::plot {
class Model;
// Zoom & Pan (issue #96): the window shown of a Y-T record, view only (the capture, its timebase and
// the record stay as they are). One factor, zoom / zoomOut with the other at 1, in powers of two from
// 1/MaximumZoomOut to MaximumZoom. Bounded by the record: zoom in stops before the window holds fewer
// than MinimumIntervals sample intervals; zoom out takes only a factor whose whole span the record
// holds (issue #109: a record a few percent longer than the screen has no 1/2x). Each step keeps the
// pan within TraceMove::panLimits.
class ViewZoom {
public:
    static constexpr unsigned MinimumIntervals = 10; // One per division.
    enum class Step : uint8_t { Changed, Limit, Unavailable };
    // A finite record on show in Y-T or Y-T+FFT (not rolling, X-Y or FFT).
    static bool available(const Options &options, const Model &model);
    // direction > 0 zooms in, < 0 out; `options` is changed only on Changed.
    static Step step(Options &options, int direction, const Model &model);
    // 1x, the pan at the trigger reference; true when anything changed.
    static bool reset(Options &options);
    // Zoom and pan are at the reset view.
    static bool home(const Options &options) { return options.zoom == 1 && options.zoomOut == 1 && !options.panPpb; }
    // "4x", "1x", "1/8x".
    static void factor(const Options &options, char *output, size_t capacity);
    // The record holds the span `options` asks for (Viewport::requestedSpanQ16), up to the span's
    // fixed-point rounding: the window is not cut short by the record. A file or a time/div change from
    // before issue #109 can leave a zoom out that does not fit; Viewport then shows the whole record.
    static bool fits(const scope::FrameMetadata &frame, const Options &options);
    // The factor as drawn: factor(), or "all" for a zoom out the record on show cannot fill.
    static void shownFactor(const Options &options, const Model &model, char *output, size_t capacity);
    // The persistent view marker away from Zoom & Pan (issue #111), empty at home: "ZOOM 4x", "ZOOM 1/2x",
    // "ZOOM all" for a zoom out the record cannot fill, "PAN" for a pan alone, joined as "ZOOM 4x PAN".
    static void marker(const Options &options, const Model &model, char *output, size_t capacity);
};
} // namespace openosc::plot
