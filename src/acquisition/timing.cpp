#include "openosc/acquisition/capture.hpp"
#include "openosc/settings/run_mode.hpp"
#include <cmath>
#include <numeric>

namespace openosc::scope {
bool CaptureTiming::calculate(uint32_t clock, Rational requested, CaptureTiming &output) {
    return calculate(clock, adcDividerAt(clock), requested, output);
}
bool CaptureTiming::points(uint32_t clock, Rational pointRate, uint32_t pointsPerTransfer, CaptureTiming &output,
                           Rational &actual) {
    Rational transfers, points;
    CaptureTiming candidate;
    const auto divider = adcDividerAt(clock);
    const auto period = pointsPerTransfer == 2 ? (divider + 1U) & ~1U : divider;
    if ((pointsPerTransfer != 1 && pointsPerTransfer != 2) || !pointRate.scaled(1, pointsPerTransfer, transfers))
        return false;
    if (period != divider && transfers.value() > double(clock) / period) transfers = {clock, period};
    if (!calculate(clock, period, transfers, candidate) || !candidate.sampleRate.scaled(pointsPerTransfer, 1, points))
        return false;
    output = candidate; actual = points;
    return true;
}
bool CaptureTiming::calculate(uint32_t clock, uint32_t adcDivider, Rational requested, CaptureTiming &output) {
    // One packed CH1/CH2 word per TIM1 period; four staggered DMA requests.
    // This is the per-channel rate, not the sum of both channel lanes. The fast branch's ADC period
    // is `adcDivider` ticks (2, 3 or 4), so the ADCs never run faster than clock / adcDivider.
    if (!clock || adcDivider < 2 || adcDivider > 4 || !requested.valid() || requested.value() > double(clock) / adcDivider)
        return false;
    CaptureTiming candidate;
    candidate.period = uint16_t(adcDivider);
    const uint64_t ticks = (uint64_t(clock)*requested.denominator + requested.numerator-1) /
                           requested.numerator;
    if (ticks > adcDivider*65536ULL) {
        candidate.period = 20;
        if (ticks > 20*65536ULL) {
            const uint64_t period = ((ticks+65535)/65536+1)&~1ULL;
            if (period > 16384) return false; // TIM3 ARR = 4*period-1 is 16-bit.
            candidate.period=uint16_t(period);
        }
    }
    const uint64_t denominator = uint64_t(candidate.period)*requested.numerator;
    // Never sample faster than the planned record can cover at this time/div.
    const uint64_t divisor = (uint64_t(clock)*requested.denominator + denominator-1)/denominator;
    if (!divisor || divisor > 65536)
        return false;
    candidate.prescaler = divisor - 1;
    const uint32_t divider = divisor * candidate.period;
    const uint32_t common = std::gcd(clock, divider);
    candidate.sampleRate = {clock / common, divider / common};
    output = candidate;
    return true;
}
} // namespace openosc::scope
