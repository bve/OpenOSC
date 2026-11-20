#include "openosc/acquisition/analysis.hpp"
#include <algorithm>

namespace openosc::scope {
namespace {
// The sides of one edge: it starts on or below the level (rising; above, falling), crosses past it,
// and with reject is armed by a code at or beyond armLevel() on the starting side.
struct Sides {
    bool rising;
    int level, arm;
    explicit Sides(const TriggerSettings &s) : rising(s.edge == Edge::Rising), level(s.level), arm(s.armLevel()) {}
    bool starting(int v) const { return rising ? v <= level : v >= level; }
    bool arming(int v) const { return rising ? v <= arm : v >= arm; }
};
// Whether the starting-side run that ends at `last` holds an arming code, from `last` back to the
// run's start or the record's first point (a run before a window: needed once per window at most).
bool armedBefore(const FrameView &frame, Channel channel, const Sides &sides, uint32_t last) {
    for (uint32_t i = last + 1; i-- > 0;) {
        int16_t v;
        if (!frame.signedCode(channel, i, v) || !sides.starting(v)) return false;
        if (sides.arming(v)) return true;
    }
    return false;
}
// Edges in [first, last] (index i means samples i-1 -> i), the one nearest target, ties to the
// lower index: exactly what a single forward pass over the same indices keeps, the arming of a run
// begun before the window included. `earliest`: the first edge, returned at once (find()).
template<bool StablePair = false>
bool scan(const FrameView &frame, const TriggerSettings &s, uint32_t first, uint32_t last, uint32_t target,
          uint32_t &index, uint32_t &distance, bool earliest = false) {
    const Sides sides(s);
    auto reader = frame.cursor(s.channel, first - 1);
    int16_t previous;
    if (!reader.next(previous)) return false;
    // armed: an arming code in the current starting-side run; known: that run began in the window.
    bool armed = sides.starting(previous) && sides.arming(previous), known = !sides.starting(previous);
    bool found = false;
    for (uint32_t i = first; i <= last; ++i) {
        int16_t value;
        if (!reader.next(value)) return false;
        if (sides.starting(previous) && !sides.starting(value)) {
            if (!armed && !known) armed = armedBefore(frame, s.channel, sides, first - 1);
            if (armed && earliest) { index = i; return true; }
            const uint32_t delta = i > target ? i - target : target - i;
            if (armed && delta < distance) {
                int16_t before, after;
                if (!StablePair || (frame.signedCode(s.channel, i - 2, before) && sides.starting(before) &&
                                    frame.signedCode(s.channel, i + 1, after) && !sides.starting(after))) {
                    index = i; distance = delta; found = true;
                }
            }
        }
        if (!sides.starting(value)) { armed = false; known = true; }
        else armed = armed || sides.arming(value);
        previous = value;
    }
    return found;
}
} // namespace
bool TriggerSearch::nearestStablePair(const FrameView &frame, const TriggerSettings &settings, uint32_t target,
                                      uint32_t first, uint32_t last, uint32_t &index) {
    const uint32_t samples = frame.metadata().samples;
    if (!settings.valid() || !frame.valid() || samples < 4) return false;
    first = std::max<uint32_t>(first, 2);
    last = std::min(last, samples - 2);
    uint32_t distance = UINT32_MAX;
    return first <= last && scan<true>(frame, settings, first, last, target, index, distance);
}
bool TriggerSearch::find(const FrameView &frame, const TriggerSettings &settings, uint32_t &index) {
    if (!settings.valid() || !frame.valid() || frame.metadata().samples < 2) return false;
    uint32_t distance = UINT32_MAX;
    return scan(frame, settings, 1, frame.metadata().samples - 1, 1, index, distance, true); // Stops at the first edge.
}
// The nearest edge to target among the edges at indices first..last (the whole record by default),
// found from the target outwards: windows of radius 1024, 4096, ... clipped to first..last, until
// one holds an edge no farther than its radius (every closer index was examined) or the window is
// the whole range. The result equals one pass over every index of the range; its cost follows the
// distance to the edge, and a full pass of the range remains only to prove there is none.
bool TriggerSearch::nearest(const FrameView &frame, const TriggerSettings &settings, uint32_t target, uint32_t &index) {
    return nearest(frame, settings, target, 1, UINT32_MAX, index);
}
bool TriggerSearch::nearest(const FrameView &frame, const TriggerSettings &settings, uint32_t target, uint32_t first,
                            uint32_t last, uint32_t &index) {
    if (!settings.valid() || !frame.valid()) return false;
    const uint32_t samples = frame.metadata().samples;
    if (samples < 2) return false;
    first = std::max<uint32_t>(first, 1); last = std::min(last, samples - 1); // Edge i: samples i-1 -> i.
    if (first > last) return false;
    target = std::clamp(target, first, last);
    for (uint64_t radius = 1024;; radius *= 4) {
        const uint32_t from = uint32_t(std::max<int64_t>(first, int64_t(target) - int64_t(radius)));
        const uint32_t to = uint32_t(std::min<uint64_t>(last, target + radius));
        uint32_t distance = UINT32_MAX, at = 0;
        const bool found = scan(frame, settings, from, to, target, at, distance);
        if ((found && distance <= radius) || (from == first && to == last)) {
            if (found) index = at;
            return found;
        }
    }
}
} // namespace openosc::scope
