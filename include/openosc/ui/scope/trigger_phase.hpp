#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/ui/scope/options.hpp"

namespace openosc::plot {
// GitHub #34: a triggered record's index is the first sample past the level (TriggerSearch: sample
// i - 1 on the starting side, i past it), so the real crossing lies up to one sample before it and
// moves from record to record. At 100 MS/s a 20 MHz sine has 5 samples a period: a trace locked to
// the index jumps up to a fifth of a period between frames.
//
// before() is where the curve the projection draws (Source::interpolate with the view's interpolation:
// linear, sinc or spline, the same kernel) crosses the record's own trigger level (FrameMetadata::
// triggerEdge) between i - 1 and i, in Q16 of a sample before i: 0..65535. Bounded: a 16-step
// bisection over that one interval (every drawn curve passes through its knots, which bracket the
// level). Where the curve rings across the level more than once in that interval, the bisection
// takes one of those crossings, the same for the same record (not necessarily the first).
// 0, the sample itself as before, when the record has no known edge or no such interval
// (an edge at the record's start, a knot a bus-glitch repair moved, the source not in the record).
// It moves where the record is shown, never its samples, index or timing.
class TriggerPhase {
public:
    static constexpr unsigned Steps = 16;
    static uint32_t before(const scope::FrameView &frame, const Options &options);
};
} // namespace openosc::plot
