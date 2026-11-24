#pragma once
#include "openosc/acquisition/bode.hpp"

namespace openosc::scope {
// What a Bode sweep's points say beyond each point (GitHub #72): between two cursors, and the results a
// user reads off a frequency response. Every value comes from neighbouring usable points only:
// - usable for the gain: a valid point (a weak CH2 included: its gain is a measurement);
// - usable for the phase: a valid point with a phase (not a weak CH2);
// - a crossing lies between two neighbouring usable points (indices i and i + 1), interpolated linearly
//   in log f (the gain and the phase linear between them); nothing crosses an invalid or weak point (a
//   gap): the crossing may be hidden in it; a flat pair (both on the same value) crosses nothing, even
//   on the level (a flat 0 dB loopback has no phase margin, a flat -180 degrees no gain margin);
// - the phase between two neighbours is unwrapped to the second nearest the first (a step of at most
//   180 degrees): a wrap from -179 to +179 is a 2 degree step, not 358.
// None found: not found (the UI shows "--"), never a value from across a gap.

// The cursors' difference B - A: the frequency for any two measured points; the gain only when both have
// one (valid); the phase only when both have a phase, wrapped to (-180, 180].
struct BodeDelta {
    bool frequency = false, gain = false, phase = false;
    double hz = 0, db = 0, deg = 0;
    static BodeDelta of(const BodePoint *points, unsigned measured, unsigned a, unsigned b) { // Inline: the view's.
        BodeDelta d;
        if (!points || a >= measured || b >= measured) return d;
        const auto &p = points[a], &q = points[b];
        if ((d.frequency = p.frequencyHz > 0 && q.frequencyHz > 0)) d.hz = double(q.frequencyHz) - double(p.frequencyHz);
        if ((d.gain = p.valid() && q.valid())) d.db = double(q.gainDb) - double(p.gainDb);
        if ((d.phase = p.phased() && q.phased())) {
            d.deg = double(q.phaseDeg) - double(p.phaseDeg);
            while (d.deg > 180) d.deg -= 360;
            while (d.deg <= -180) d.deg += 360;
        }
        return d;
    }
};
// A level crossed between two neighbours: where (Hz) and the other quantity there (dB or degrees).
struct BodeCrossing {
    bool found = false;
    float hz = 0, value = 0;
};
// The response's centre and -3 dB bounds (GitHub #122), shared by the results and the plot's markers.
// - A notch: the lowest valid gain has valid points on both sides that rise at least NotchDb above it (the
//   smaller of the two rises), and that rise exceeds the peak's prominence (the smaller of the drops on
//   both sides of the highest gain; none on a side the sweep ends at). NotchDb leaves each -3 dB level at
//   least 3 dB above the floor, so ripple or noise under 3 dB cannot give a notch its bounds. Its centre is
//   the lowest gain; each bound is that side's passband (its highest valid gain) - 3 dB, crossed rising.
// - Else a peak (band-pass, low-pass, high-pass, flat): the centre is the highest gain; both bounds are
//   centre - 3 dB, crossed falling.
// Equal gains: the lowest frequency. Each bound is the first crossing outwards from the centre between
// neighbouring valid points; a gap or the sweep's end before it: none (count). Nothing is extrapolated: a
// partial sweep shows only what its measured points cross, and is classified by them.
struct BodeShape {
    static constexpr float NotchDb = 6;
    uint16_t center = 0, lower = 0, upper = 0; // lower, upper: the crossing pair's outer point, or count.
    float lowerLevel = 0, upperLevel = 0;      // The gain each bound crosses.
    bool found = false, notch = false;         // found: a valid point (the centre).
    static BodeShape of(const BodePoint *points, unsigned count);
};
// The results of a (finished or partial) sweep. Selection rules where a quantity crosses more than once:
// - Peak (notch): the centre of BodeShape, the highest (lowest) valid gain; `notch` says which.
// - -3 dB bounds: BodeShape's lower and upper, interpolated between the crossing's two points (value: the
//   gain crossed). Bandwidth = upper - lower, only when both are found (a low-pass swept from above its
//   passband's start has no lower bound: its upper bound is the -3 dB frequency, the bandwidth "--"); of a
//   notch, its width between the bounds.
// - Gain margin: at a phase crossover (the unwrapped phase through -180 + k 360, between neighbours with a
//   phase), -gain there in dB; of several, the smallest margin (the worst case; equal: the lowest
//   frequency). Positive: the gain is below 0 dB where the phase reaches -180.
// - Phase margin: at a gain crossover (the gain through 0 dB, between neighbours with a phase), 180 + the
//   phase there, wrapped to (-180, 180]; of several, the smallest (equal: the lowest frequency).
struct BodeResults {
    bool peak = false, notch = false; // peak: a centre was found; notch: it is a notch's (the lowest gain).
    unsigned peakIndex = 0;
    float peakHz = 0, peakDb = 0;
    BodeCrossing lower, upper;     // value: the gain there (peak - 3 dB).
    bool bandwidth = false;
    float bandwidthHz = 0;
    BodeCrossing gainMargin;       // hz: the phase crossover; value: the margin in dB.
    BodeCrossing phaseMargin;      // hz: the gain crossover; value: the margin in degrees.
    static BodeResults of(const BodePoint *points, unsigned measured);
};
} // namespace openosc::scope
