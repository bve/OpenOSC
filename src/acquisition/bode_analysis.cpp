#include "openosc/acquisition/bode_analysis.hpp"
#include <cmath>

namespace openosc::scope {
namespace {
double wrap(double degrees) {
    while (degrees > 180) degrees -= 360;
    while (degrees <= -180) degrees += 360;
    return degrees;
}
bool gained(const BodePoint &p) { return p.valid() && p.frequencyHz > 0; }
bool phased(const BodePoint &p) { return p.phased() && p.frequencyHz > 0; }
// Where `level` lies between a and b (0 at a, 1 at b), when it does: between them or on either. A flat
// pair (a == b) crosses nothing, even on the level: it only touches it, and an exact hit on a point is
// found by the sloped pair on its other side.
bool between(double a, double b, double level, double &t) {
    if (a == b) return false;
    if ((level < a && level < b) || (level > a && level > b)) return false;
    t = (level - a) / (b - a);
    return true;
}
// log-f interpolation between two points at fraction t.
double frequencyAt(const BodePoint &p, const BodePoint &q, double t) {
    return std::exp(std::log(double(p.frequencyHz)) + t * (std::log(double(q.frequencyHz)) - std::log(double(p.frequencyHz))));
}
// The smaller of two margins (equal: the lower frequency).
void keepWorst(BodeCrossing &kept, double hz, double value) {
    if (!kept.found || value < kept.value || (value == kept.value && hz < kept.hz)) kept = {true, float(hz), float(value)};
}
} // namespace

BodeShape BodeShape::of(const BodePoint *points, unsigned count) {
    BodeShape s;
    s.center = s.lower = s.upper = uint16_t(count);
    if (!points) return s;
    unsigned high = count, low = count;
    for (unsigned i = 0; i < count; ++i)
        if (gained(points[i])) {
            if (high == count || points[i].gainDb > points[high].gainDb) high = i;
            if (low == count || points[i].gainDb < points[low].gainDb) low = i;
        }
    if (high == count) return s;
    // The highest or lowest valid gain on one side of `at` (none: the value at `at`).
    const auto extreme = [&](unsigned at, int direction, bool highest) {
        float value = points[at].gainDb;
        for (int i = int(at) + direction; i >= 0 && i < int(count); i += direction)
            if (gained(points[i])) value = highest ? std::fmax(value, points[i].gainDb) : std::fmin(value, points[i].gainDb);
        return value;
    };
    const float below = extreme(low, -1, true), above = extreme(low, 1, true);
    const float rise = std::fmin(below, above) - points[low].gainDb;
    const float drop = points[high].gainDb - std::fmax(extreme(high, -1, false), extreme(high, 1, false));
    s.found = true;
    s.notch = rise >= NotchDb && rise > drop;
    s.center = uint16_t(s.notch ? low : high);
    s.lowerLevel = s.notch ? below - 3 : points[high].gainDb - 3;
    s.upperLevel = s.notch ? above - 3 : s.lowerLevel;
    // Outwards from the centre through neighbouring valid points to the first crossing of the level.
    const auto bound = [&](int direction, float level) {
        for (int i = s.center; ; i += direction) {
            const int j = i + direction;
            if (j < 0 || j >= int(count) || !gained(points[j])) return uint16_t(count); // The sweep's end, or a gap.
            double t;
            const bool beyond = s.notch ? points[j].gainDb >= level : points[j].gainDb <= level;
            if (beyond && between(points[i].gainDb, points[j].gainDb, level, t)) return uint16_t(j);
        }
    };
    s.lower = bound(-1, s.lowerLevel);
    s.upper = bound(1, s.upperLevel);
    return s;
}

BodeResults BodeResults::of(const BodePoint *points, unsigned measured) {
    BodeResults r;
    if (!points) return r;
    const auto shape = BodeShape::of(points, measured);
    if (shape.found) {
        const auto &c = points[shape.center];
        r.peak = true; r.notch = shape.notch; r.peakIndex = shape.center; r.peakHz = c.frequencyHz; r.peakDb = c.gainDb;
        const auto crossing = [&](unsigned outer, int direction, float level, BodeCrossing &out) {
            if (outer >= measured) return;
            const auto &p = points[outer - direction], &q = points[outer];
            double t;
            if (between(p.gainDb, q.gainDb, level, t)) out = {true, float(frequencyAt(p, q, t)), level};
        };
        crossing(shape.lower, -1, shape.lowerLevel, r.lower);
        crossing(shape.upper, 1, shape.upperLevel, r.upper);
        if (r.lower.found && r.upper.found) { r.bandwidth = true; r.bandwidthHz = r.upper.hz - r.lower.hz; }
    }
    // Margins: between neighbours that both have a phase, the second unwrapped nearest the first.
    for (unsigned i = 0; i + 1 < measured; ++i) {
        const auto &p = points[i], &q = points[i + 1];
        if (!phased(p) || !phased(q)) continue;
        const double a = p.phaseDeg, b = a + wrap(double(q.phaseDeg) - a);
        const double ga = p.gainDb, gb = q.gainDb;
        // Phase crossovers: -180 + k 360 in [min(a, b), max(a, b)].
        const double low = std::fmin(a, b), high = std::fmax(a, b);
        for (double level = std::ceil((low - 180) / 360) * 360 + 180; level <= high; level += 360) {
            double t;
            if (!between(a, b, level, t)) continue;
            keepWorst(r.gainMargin, frequencyAt(p, q, t), -(ga + t * (gb - ga)));
        }
        // The gain crossover.
        double t;
        if (between(ga, gb, 0, t)) keepWorst(r.phaseMargin, frequencyAt(p, q, t), wrap(180 + a + t * (b - a)));
    }
    return r;
}
} // namespace openosc::scope
