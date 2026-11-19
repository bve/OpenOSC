#pragma once
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/ui/scope/options.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace openosc::plot {
// Counter overlay: mean of N settled readings; last digit at the decade of max(2 Hz, 1.4 ppm)/sqrt(N). No %f (nano).
class CounterText {
public:
    static constexpr double FloorHz = 2, ScatterPpm = 1.4, Hysteresis = 1.1;
    static constexpr int MaximumDigits = 7, UncertainDigits = 3;
    // Empty when off; else "1.000000MHz", "no signal", "--", an uncertain or changing count "1.01MHz?".
    // context: what the readings depend on besides the options (the caller's clock profile and Scope
    // entry, plan S13): a change starts the mean again, as a new channel, average or correction does.
    void update(const scope::CounterSnapshot &snapshot, const CounterOptions &o, char *out, size_t size, uint32_t context = 0) {
        if (!size) return;
        out[0] = 0;
        if (!o.valid() || !o.source) { reset(); return; }
        const unsigned c = o.source - 1U;
        if (c != channel_ || o.average != average_ || o.ppm != ppm_ || context != context_) {
            reset(); channel_ = c; average_ = o.average; ppm_ = o.ppm; context_ = context;
        }
        const auto &r = snapshot.channels[c];
        if (snapshot.sequence != sequence_) {
            sequence_ = snapshot.sequence;
            if (r.settled()) { values_[next_] = r.hertz; next_ = (next_ + 1U) % average_; count_ = std::min<unsigned>(count_ + 1U, average_); }
            else count_ = next_ = 0; // An unsettled count restarts the mean.
        }
        if (!r.valid) { std::snprintf(out, size, "--"); return; }
        const double correction = 1 + o.ppm * 1e-6;
        if (!r.settled() || !count_ || r.quality == scope::CounterQuality::Unverified) {
            const double f = r.hertz * correction;
            if (!(f > 0) || !std::isfinite(f)) { std::snprintf(out, size, "no signal"); return; }
            format(f, int(std::floor(std::log10(f))) - (UncertainDigits - 1), UncertainDigits, "?", out, size);
            return;
        }
        double mean = 0;
        for (unsigned i = 0; i < count_; ++i) mean += values_[i];
        const double f = mean / count_ * correction;
        if (!(f > 0) || !std::isfinite(f)) { std::snprintf(out, size, "no signal"); return; }
        const double scatter = std::max(FloorHz, ScatterPpm * 1e-6 * f) / std::sqrt(double(count_));
        // The step's decade, kept until the scatter is 10% past either edge of (10^(e-1), 10^e].
        const int step = int(std::ceil(std::log10(scatter) - 1e-9));
        if (!stepKnown_ || scatter > std::pow(10.0, step_) * Hysteresis || scatter * Hysteresis < std::pow(10.0, step_ - 1))
            step_ = step;
        stepKnown_ = true;
        const int decade = int(std::floor(std::log10(f)));
        format(f, std::clamp(step_, decade - (MaximumDigits - 1), decade), MaximumDigits, "", out, size); // At least one digit.
    }
    // f rounded to a whole number of 10^step Hz (step >= -1), at most `digits` digits after rounding (a
    // carry into the next decade drops one), in Hz, kHz or MHz, trailing zeros kept.
    static void format(double f, int step, int digits, const char *mark, char *out, size_t size) {
        step = std::max(step, -1);
        uint64_t steps = uint64_t(std::llround(f / std::pow(10.0, step)));
        uint64_t limit = 1;
        for (int i = 0; i < digits; ++i) limit *= 10;
        if (steps >= limit) steps = uint64_t(std::llround(f / std::pow(10.0, ++step)));
        uint64_t tenths = steps;
        for (int e = step; e >= 0; --e) tenths *= 10; // Now f in tenths of a Hz.
        static constexpr const char *units[] = {"Hz", "kHz", "MHz"};
        const unsigned unit = tenths >= 10000000ULL ? 2 : tenths >= 10000ULL ? 1 : 0;
        const int decimals = std::max(0, int(unit) * 3 - step);
        uint64_t value = tenths, scale = 1; // value: the unit's 10^-decimals parts.
        for (int e = int(unit) * 3 - decimals; e >= 0; --e) value /= 10;
        for (int e = 0; e < decimals; ++e) scale *= 10;
        const unsigned long integer = (unsigned long)(value / scale), fraction = (unsigned long)(value % scale);
        if (decimals) std::snprintf(out, size, "%lu.%0*lu%s%s", integer, decimals, fraction, units[unit], mark);
        else std::snprintf(out, size, "%lu%s%s", integer, units[unit], mark);
    }
    void reset() { count_ = next_ = 0; stepKnown_ = false; }
private:
    double values_[10]{};
    uint32_t sequence_ = 0;
    unsigned channel_ = 0, average_ = 1, count_ = 0, next_ = 0;
    int step_ = 0, ppm_ = 0;
    uint32_t context_ = 0;
    bool stepKnown_ = false;
};
} // namespace openosc::plot
