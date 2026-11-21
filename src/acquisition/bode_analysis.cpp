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

BodeResults BodeResults::of(const BodePoint *points, unsigned measured) {
    BodeResults r;
    if (!points) return r;
    for (unsigned i = 0; i < measured; ++i)
        if (gained(points[i]) && (!r.peak || points[i].gainDb > r.peakDb)) {
            r.peak = true; r.peakIndex = i; r.peakHz = points[i].frequencyHz; r.peakDb = points[i].gainDb;
        }
    if (r.peak) { // -3 dB: outwards from the peak through neighbouring valid points only.
        const double level = double(r.peakDb) - 3;
        const auto search = [&](int direction, BodeCrossing &out) {
            for (int i = int(r.peakIndex); ; i += direction) {
                const int j = i + direction;
                if (j < 0 || j >= int(measured) || !gained(points[j])) return; // The sweep's end, or a gap.
                double t;
                if (points[j].gainDb <= level && between(points[i].gainDb, points[j].gainDb, level, t)) {
                    out = {true, float(frequencyAt(points[i], points[j], t)), float(level)};
                    return;
                }
            }
        };
        search(-1, r.lower);
        search(1, r.upper);
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
