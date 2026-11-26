#pragma once
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/ranges.hpp"
#include "openosc/measurement/dmm/resolution.hpp"
#include <cmath>
#include <cstdint>

namespace openosc::dmm {
// A DMM reading that moved enough to count as activity (GitHub #169, Settings > Power > Wake on
// measurement): a fresh valid reading at least Percent of its range's full scale (Ranges::current, the
// range the packet reports) from the reference, the last reading that counted or the baseline. The
// reference then moves to it, so a slow drift counts once each time it has gone that far, and quiet
// packets or a reading seen again (the same sequence) never count again.
// The comparison is in the reading's own counts (Resolution::quantum), so the threshold is exact:
// ceil(full scale counts / 10), 2500 counts of the 2.5 V range, 1000 of 9.999 nF.
// A new baseline, without activity: the first reading, another function, range, input or meter REL,
// and the first reading after anything not usable. Not usable (the baseline is dropped): a reading
// the caller marks so (stale, an unconfirmed selection, HOLD of either kind, Type K), no value (OL,
// a blank, an unreadable one), no range table (continuity, the diode, temperature), above its range,
// and the DC/AC millivolt functions, whose open inputs pick up static and mains (the issue's reports).
class ActivityDetector {
public:
    static constexpr unsigned Percent = 10;
    // True when `reading` is activity. `usable`: the caller's verdict on its freshness and display.
    bool observe(const Result &reading, bool usable) {
        // An unusable reading drops the baseline at once, even when its packet was seen before (the
        // last packet going stale, HOLD): that packet then stays seen, so it never reseeds or counts.
        if (!usable) { armed_ = false; seen_ = true; sequence_ = reading.sequence; return false; }
        if (seen_ && reading.sequence == sequence_) return false; // Each packet once.
        seen_ = true;
        sequence_ = reading.sequence;
        Signature signature;
        int32_t counts = 0, threshold = 0;
        if (!measure(reading, signature, counts, threshold)) { armed_ = false; return false; }
        if (!armed_ || !(signature == signature_)) {
            armed_ = true; signature_ = signature; reference_ = counts;
            return false;
        }
        const int32_t moved = counts > reference_ ? counts - reference_ : reference_ - counts;
        if (moved < threshold) return false;
        reference_ = counts;
        return true;
    }
    void reset() { seen_ = armed_ = false; }
    bool armed() const { return armed_; } // A baseline is held (tests).

    static bool excluded(Function function) { return function == Function::MillivoltsDc || function == Function::MillivoltsAc; }

private:
    struct Signature { // Compact: the controller keeps one detector (DTCM).
        uint8_t function = uint8_t(Function::Unknown), range = 0;
        bool current = false, relative = false;
        bool operator==(const Signature &o) const {
            return function == o.function && range == o.range && current == o.current && relative == o.relative;
        }
    };
    static bool measure(const Result &reading, Signature &signature, int32_t &counts, int32_t &threshold) {
        if (!reading.primary.valid() || !std::isfinite(reading.primary.si)) return false;
        const Function function = Functions::identify(reading);
        if (function == Function::Unknown || excluded(function)) return false;
        unsigned index = 0;
        const RangeInfo *range = Ranges::current(reading, &index);
        const double quantum = Resolution::of(reading).quantum();
        if (!range || !(quantum > 0) || !std::isfinite(quantum) || !std::isfinite(range->maximumSi)) return false;
        // Above the range in whole counts: the decoded value and the range literal may differ by an
        // ULP at full scale (9.999 mF), which rounding to counts removes.
        const double full = std::round(range->maximumSi / quantum);
        const double scaled = std::round(reading.primary.si / quantum);
        if (!(full >= Percent && full < 1e9) || !std::isfinite(scaled) || !(std::fabs(scaled) <= full)) return false;
        counts = int32_t(scaled); // |scaled| <= full < 1e9.
        threshold = (int32_t(full) + int32_t(Percent) - 1) / int32_t(Percent);
        signature.function = uint8_t(function);
        signature.range = uint8_t(index);
        signature.current = reading.currentInput;
        signature.relative = reading.peerRelative;
        return true;
    }
    int32_t reference_ = 0;
    uint32_t sequence_ = 0;
    Signature signature_;
    bool seen_ = false, armed_ = false;
};
} // namespace openosc::dmm
