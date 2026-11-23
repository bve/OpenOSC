#pragma once
#include "openosc/board/current.hpp"
#include "openosc/data/units.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include <algorithm>

namespace openosc::dmm {
// The counts scale the bar and the dial draw: full deflection at the controller's display maximum, so
// no valid reading pegs them (the decoder's 66000 is overload, never a reading). A minor tick every
// 1000 counts, a labelled one every `major` thousand. ZT703S 25000 (0..25 by 5), as before.
// ZT706S 66000 (0..66 by 11): the manual's 66000 counts. The factory 706S dial (08042778) clamps
// at 62000, which would peg valid readings of 62000..65999 before overload; not copied.
// Capacitance is a 4-digit function on both models (every range 9.999/99.99/999.9, both manuals):
// its nominal scale is 10000 counts (0..10 by 2, whole labelled ticks at the range's nominal end) and
// 9999 is the largest valid reading, 0.9999 of the scale; overload pegs at 10000. Before, it used
// the board's scale, where 9999 reached only 0.40 (703) or 0.15 (706) of the dial.
struct CountScale {
    int fullScale, major;
    int minimum = 0, tickStep = 1000;
    constexpr int ticks() const { return fullScale / tickStep; }
    constexpr int labels() const { return ticks() / major + 1; }
    constexpr int tickValue(int tick) const { return minimum + tick * tickStep; }
    constexpr bool operator==(const CountScale &other) const {
        return fullScale == other.fullScale && major == other.major && minimum == other.minimum && tickStep == other.tickStep;
    }
    constexpr bool operator!=(const CountScale &other) const { return !(*this == other); }
    static constexpr CountScale of(board::DmmCounts counts) {
        return counts == board::DmmCounts::Counts66000 ? CountScale{66000, 11} : CountScale{25000, 5};
    }
};
inline constexpr CountScale BoardScale = CountScale::of(board::Current::Features.dmmCounts);
inline constexpr CountScale CapacitanceScale{10000, 2};
// Diode test (GitHub #116): a nominal 0..3.3 V on both models, whatever decimals the controller's
// display uses. Its counts are millivolts of the physical reading (DiodeDecimals), a minor tick every
// 0.1 V and a number every 1.1 V (0, 1.1, 2.2, 3.3); overload pegs at 3.3 V.
inline constexpr CountScale DiodeScale{3300, 11, 0, 100};
inline constexpr unsigned DiodeDecimals = 3;
// The scale of a reading of `quantity` on this board: its function's own counts.
constexpr CountScale scaleFor(Quantity quantity) { return quantity == Quantity::Capacitance ? CapacitanceScale : BoardScale; }
// The same for a packet: the diode by its kind (with or without the beeper flag), also before it
// has a number (OL, no reading), so the first diode packet already selects its scale.
constexpr CountScale scaleFor(const Result &result) {
    return functionKind(result.kind) == Kind::Diode ? DiodeScale : scaleFor(result.primary.quantity);
}
// The most labels any of this board's scales has (fixed label storage in the bar and the dial).
inline constexpr int MaxScaleLabels = std::max({BoardScale.labels(), CapacitanceScale.labels(), DiodeScale.labels()});
static_assert(BoardScale.fullScale % 1000 == 0 && BoardScale.ticks() % BoardScale.major == 0, "whole labelled ticks");
static_assert(CapacitanceScale.fullScale % 1000 == 0 && CapacitanceScale.ticks() % CapacitanceScale.major == 0, "whole labelled ticks");
static_assert(DiodeScale.fullScale % DiodeScale.tickStep == 0 && DiodeScale.ticks() % DiodeScale.major == 0, "labelled ends");
} // namespace openosc::dmm
