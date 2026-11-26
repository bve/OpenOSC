#pragma once
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/ui/scope/counter_estimate.hpp"
#include "openosc/ui/scope/options.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace openosc::plot {
// Counter overlay: mean of N settled readings; last digit at the decade of max(2 Hz, 1.4 ppm)/sqrt(N) for
// the gate count, of 1.4 ppm/sqrt(N) for a timed one (CounterReading::timed, GitHub #173: at most
// TimedDigits, to 10 uHz). A timed and a gate reading never share a mean. No %f (nano).
class CounterText {
public:
    static constexpr double FloorHz = 2, ScatterPpm = 1.4, Hysteresis = 1.1;
    static constexpr int MaximumDigits = 7, TimedDigits = 6, UncertainDigits = 3;
    static constexpr int GateStep = -1, TimedStep = -5; // The finest digit: 0.1 Hz, 10 uHz.
    // Empty when off; else "1.000000MHz", "no signal", "--", an uncertain or changing count "1.01MHz?",
    // or with No edges counted > Trace f (~) and a Clean count of none, the record's own f: "20.0MHz~"
    // (CounterEstimate, its digits, no correction).
    // context: what the readings depend on besides the options (the caller's clock profile and Scope
    // entry, plan S13): a change starts the mean again, as a new channel, average or correction does.
    void update(const scope::CounterSnapshot &snapshot, const CounterOptions &o, char *out, size_t size, uint32_t context = 0,
                CounterEstimate::Value estimate = {}) {
        if (!size) return;
        out[0] = 0;
        if (!o.valid() || !o.source) { reset(); return; }
        const unsigned c = o.source - 1U;
        if (c != channel_ || o.average != average_ || o.ppm != ppm_ || context != context_) {
            reset(); channel_ = c; average_ = o.average; ppm_ = o.ppm; context_ = context;
        }
        const auto &r = snapshot.channels[c];
        if (!sequenceKnown_ || snapshot.updates[c] != sequence_) {
            // A count of none, an unsettled one or one of the other kind restarts the mean: no falling
            // value with full digits once a signal stops being counted.
            if (r.timed != timed_) { reset(); timed_ = r.timed; }
            sequence_ = snapshot.updates[c];
            sequenceKnown_ = true;
            if (r.settled() && r.hertz > 0) { values_[next_] = r.hertz; next_ = (next_ + 1U) % average_; count_ = std::min<unsigned>(count_ + 1U, average_); }
            else count_ = next_ = 0;
        }
        if (o.estimate && estimate.valid() && CounterEstimate::none(r)) {
            const int digits = std::clamp(int(estimate.digits), 1, UncertainDigits);
            format(estimate.hertz, int(std::floor(std::log10(estimate.hertz))) - (digits - 1), digits, "~", out, size, GateStep);
            return;
        }
        if (!r.valid) { std::snprintf(out, size, "--"); return; }
        const double correction = 1 + o.ppm * 1e-6;
        const int finest = r.timed ? TimedStep : GateStep;
        if (!r.settled() || !count_ || r.quality == scope::CounterQuality::Unverified) {
            const double f = r.hertz * correction;
            if (!(f > 0) || !std::isfinite(f)) { std::snprintf(out, size, "no signal"); return; }
            format(f, int(std::floor(std::log10(f))) - (UncertainDigits - 1), UncertainDigits, "?", out, size, finest);
            return;
        }
        double mean = 0;
        for (unsigned i = 0; i < count_; ++i) mean += values_[i];
        const double f = mean / count_ * correction;
        if (!(f > 0) || !std::isfinite(f)) { std::snprintf(out, size, "no signal"); return; }
        const double scatter = (r.timed ? ScatterPpm * 1e-6 * f : std::max(FloorHz, ScatterPpm * 1e-6 * f)) / std::sqrt(double(count_));
        // The step's decade, kept until the scatter is 10% past either edge of (10^(e-1), 10^e].
        const int step = int(std::ceil(std::log10(scatter) - 1e-9));
        if (!stepKnown_ || scatter > std::pow(10.0, step_) * Hysteresis || scatter * Hysteresis < std::pow(10.0, step_ - 1))
            step_ = step;
        stepKnown_ = true;
        const int decade = int(std::floor(std::log10(f)));
        const int digits = r.timed ? TimedDigits : MaximumDigits;
        format(f, std::clamp(step_, decade - (digits - 1), decade), digits, "", out, size, finest); // At least one digit.
    }
    // f rounded to a whole number of 10^step Hz (step >= finest), at most `digits` digits after rounding
    // (a carry into the next decade drops one), in Hz, kHz or MHz, trailing zeros kept.
    static void format(double f, int step, int digits, const char *mark, char *out, size_t size, int finest = GateStep) {
        step = std::max(step, finest);
        uint64_t steps = uint64_t(std::llround(f / std::pow(10.0, step)));
        if (steps >= power(unsigned(digits))) steps = uint64_t(std::llround(f / std::pow(10.0, ++step)));
        // f >= 10^(3 unit) in whole steps: steps >= 10^(3 unit - step), always when that is below 1.
        const auto reaches = [&](int unit) { const int e = 3 * unit - step; return e <= 0 || steps >= power(unsigned(e)); };
        static constexpr const char *units[] = {"Hz", "kHz", "MHz"};
        const int unit = reaches(2) ? 2 : reaches(1) ? 1 : 0;
        const int decimals = std::max(0, unit * 3 - step);
        const uint64_t value = step > unit * 3 ? steps * power(unsigned(step - unit * 3)) : steps; // The unit's 10^-decimals.
        const uint64_t scale = power(unsigned(decimals));
        const unsigned long integer = (unsigned long)(value / scale), fraction = (unsigned long)(value % scale);
        if (decimals) std::snprintf(out, size, "%lu.%0*lu%s%s", integer, decimals, fraction, units[unit], mark);
        else std::snprintf(out, size, "%lu%s%s", integer, units[unit], mark);
    }
    void reset() { count_ = next_ = 0; stepKnown_ = sequenceKnown_ = false; }
private:
    static uint64_t power(unsigned e) { uint64_t p = 1; while (e--) p *= 10; return p; }
    double values_[10]{};
    uint32_t sequence_ = 0;
    unsigned channel_ = 0, average_ = 1, count_ = 0, next_ = 0;
    int step_ = 0, ppm_ = 0;
    uint32_t context_ = 0;
    bool stepKnown_ = false, sequenceKnown_ = false, timed_ = false;
};
} // namespace openosc::plot
