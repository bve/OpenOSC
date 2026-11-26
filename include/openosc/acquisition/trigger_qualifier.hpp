#pragma once
#include "openosc/acquisition/analysis.hpp"

namespace openosc::scope {
// The qualified triggers (TriggerType Width, Runt and Transition, GitHub #133) over one channel's codes:
// one forward pass with a few integers of state, no copy of the record and no history of its samples.
//
// Codes are oriented so that every polarity reads as the positive one: a negative polarity takes
// -code against the negated levels, which gives exactly the falling edge's comparisons (from v >= L to
// v < L). Points are record indices; the trigger point of a candidate is the first point past the
// crossing that decides it (TriggerSettings::tag), as an edge's index is.
// - Width: a pulse is the run of points above the level; its first point i, its first point back j,
//   its width j - i points; trigger point j.
// - Runt: a pulse above the low level that comes back over it without a point past the high level;
//   width j - i as above; trigger point j.
// - Transition: from the first point above the low level (i) to the first point above the high level
//   (k) with no return to the low level between; time k - i points; trigger point k.
// Times are counted in whole points at the record's own rate (FrameMetadata::samplesPerSecond, the
// rational rate the capture realized), and a limit T is compared exactly with T x rate (Limit), so a
// condition holds for the measured time, which is within one sample period of the true one.
// Noise reject (TriggerSettings::reject) is the edge trigger's hysteresis: a candidate starts only once
// the signal was RejectCodes beyond its starting level, and its return over that level ends it only
// once the signal goes as far beyond it (a recrossing before then is chatter of the same candidate).
// A candidate the record cuts (begun before its first point, or still open at its end) is never
// qualified. A code at the ADC's limit counts as past any level on that side: a clipped pulse is
// never taken for a runt.
class TriggerQualifier {
public:
    // A time limit in points at a rate: T x rate = whole + a fraction (fraction: not a whole number).
    struct Limit {
        uint64_t whole = 0;
        bool fraction = false;
        bool below(uint64_t points) const { return points < whole + (fraction ? 1 : 0); } // points / rate < T
        bool above(uint64_t points) const { return points > whole; }                     // points / rate > T
    };
    static Limit limit(uint32_t ns, Rational rate);
    TriggerQualifier(const TriggerSettings &settings, Rational rate);
    // The next code in record order from the record's first point. True when a candidate qualified
    // here; point() is then its trigger point (Width and Runt: possibly before this point, while Noise
    // reject confirmed their return).
    bool feed(int16_t code);
    uint32_t point() const { return end_; }
    // Whether a candidate is still being decided (its trigger point may lie before the next point).
    bool pending() const { return phase_ == Phase::Ending && type_ != TriggerType::Transition; }
    bool qualifies(uint64_t points) const;
    // The qualified point nearest `target` among those at first..last (clamped to the record), ties to
    // the lower index, for each window at once: one pass, which stops once no window can change.
    struct Window {
        uint32_t first = 0, last = 0;
        bool found = false;
        uint32_t index = 0;
    };
    static void nearest(const FrameView &frame, const TriggerSettings &settings, uint32_t target, Window *windows,
                        unsigned count);
    static bool find(const FrameView &frame, const TriggerSettings &settings, uint32_t &index); // The first one.

private:
    enum class Phase : uint8_t { Idle, Inside, Ending, Done };
    bool past(int o) const { return o > high_ || o >= rail_; }
    TriggerType type_;
    TriggerCondition condition_;
    Limit minimum_, maximum_;
    int low_ = 0, high_ = 0, arm_ = 0, rail_ = 127, sign_ = 1;
    uint32_t at_ = 0, start_ = 0, end_ = 0;
    Phase phase_ = Phase::Idle;
    bool armed_ = false, reached_ = false;
};
} // namespace openosc::scope
