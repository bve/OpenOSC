#include "openosc/generator/waveform.hpp"
#include <cmath>
#include <cstdio>
#include <numeric>
#include <limits>

namespace openosc::generator {
namespace {
using Dac = DacCapabilities;
unsigned dacPoints(const Settings &settings,uint32_t clock) {
    const double frequency=settings.frequencyHz.value();
    const uint32_t minimum=Dac::minimumDivider(clock);
    const double rate=double(clock)/minimum; // The fastest update at this clock.
    unsigned upper=frequency<=rate/Dac::MaximumPoints ? Dac::MaximumPoints : unsigned(rate/frequency);
    if (settings.shape==Shape::Triangle) upper &= ~1U;
    if (upper<Dac::MinimumPoints) return 0;
    unsigned selected=upper;
    // Compare count/divider pairs before the full PSC/ARR search. At low rates
    // timer quantization is already tiny; one full TimerPlan search suffices.
    if (clock/(frequency*upper)<=65536) {
        double best=std::numeric_limits<double>::infinity();
        for (unsigned count=upper; count>=Dac::MinimumPoints; --count) {
            if (settings.shape==Shape::Triangle && (count&1)) continue;
            const long divider=std::lround(clock/(frequency*count));
            if (divider<long(minimum) || divider>65536) continue;
            const double error=std::abs(double(clock)/(divider*count)-frequency);
            if (error>=best) continue;
            best=error; selected=count;
            if (!error) break;
        }
    }
    return selected;
}
// TIM6's update rate for `count` samples a period: frequency x count, exact when it fits 32 bits. A
// fractional frequency's exact rate often does not (6727.487 Hz = 6727487/1000 Hz x 1024 samples; GitHub
// #38: about 6 % of fractional settings were refused although the DAC plays them): it is then the nearest
// rate whose numerator fits, within about one part in 10^9 of it, far below the timer's own PSC/ARR
// quantization (the applied frequency is reported from the plan). False only for a rate no 32-bit
// rational holds (none the DAC reaches: at most 10 MS/s).
bool updateRate(const Rational &frequency, unsigned count, Rational &rate) {
    uint64_t numerator = uint64_t(frequency.numerator) * count, denominator = frequency.denominator;
    const uint64_t common = std::gcd(numerator, denominator);
    numerator /= common; denominator /= common;
    if (numerator > UINT32_MAX) {
        const uint64_t whole = numerator / denominator;
        if (!whole || whole > UINT32_MAX) return false;
        const uint64_t scaled = UINT32_MAX / (whole + 1); // The new denominator: numerator stays below 2^32.
        const uint64_t next = whole * scaled + ((numerator % denominator) * scaled + denominator / 2) / denominator;
        const uint64_t reduced = std::gcd(next, scaled);
        numerator = next / reduced; denominator = scaled / reduced;
    }
    if (numerator > UINT32_MAX || denominator > UINT32_MAX) return false;
    rate = {uint32_t(numerator), uint32_t(denominator)};
    return true;
}
}
bool Settings::valid() const {
    return static_cast<unsigned>(shape) < ShapeCount && frequencyHz.valid() &&
           ArbitraryLimits::rate(arbitrarySamplesPerSecond) &&
           unsigned(level) <= unsigned(Level::Custom) && unsigned(squareDrive) <= unsigned(SquareDrive::Pwm) &&
           minimumCode <= maximumCode && maximumCode <= 4095 && dutyPermille <= 1000 && amplitudeMillivolts <= 5000;
}
bool DacCapabilities::accepts(const Settings &s, uint32_t timerHz) {
    if (!s.valid()) return false;
    if (s.arbitrary()) return ArbitraryLimits::rate(s.arbitrarySamplesPerSecond, timerHz); // The codes are the waveform's.
    const double millihertz = s.frequencyHz.value() * 1000;
    return millihertz >= MinimumMillihertz && millihertz <= double(maximumMillihertz(s.shape, s.level, timerHz));
}
CodeRange Levels::codes(const Settings &s) {
    if (s.level == Level::Custom) return {s.minimumCode,s.maximumCode};
    if (s.level == Level::Low) return {0,uint16_t(s.shape==Shape::Sine ? 1600 : 1500)};
    if (s.level == Level::Dedicated && s.shape==Shape::Sine && kilohertz(s))
        return {600,3600};
    return {0,4000};
}
const char *Levels::label(Level level) {
    return level == Level::Low ? "1.0" : level == Level::Custom ? nullptr : "2.5";
}
void Levels::describe(const Settings &s, char *output, size_t capacity) {
    if (!capacity) return;
    const char *caption = label(s.level);
    const auto range = codes(s);
    if (!caption) std::snprintf(output, capacity, "Custom DAC %u-%u", range.minimum, range.maximum);
    else if (pwm(s)) std::snprintf(output, capacity, "PWM logic level, factory label %sV", caption);
    else std::snprintf(output, capacity, "DAC %u-%u, factory label %sV", range.minimum, range.maximum, caption);
}
bool WaveTable::build(const Settings &settings, uint16_t *samples, size_t count) {
    if (!settings.valid() || settings.arbitrary() || !samples || count < 2 || count > 4096)
        return false; // Arbitrary samples are loaded (ArbitraryBanks), never built.
    const auto codes=Levels::codes(settings);
    const double span = codes.maximum - codes.minimum;
    constexpr double pi = 3.1415926535897932384626433832795;
    for (size_t i = 0; i < count; ++i) {
        const double phase = double(i) / count;
        double normalized = 0;
        switch (settings.shape) {
        case Shape::Sine:
            normalized = .5 + .5 * std::sin(2 * pi * phase);
            break;
        case Shape::Square:
            normalized = i < (uint64_t(count)*settings.dutyPermille+500)/1000 ? 1 : 0;
            break;
        case Shape::Triangle:
            normalized = phase < .5 ? 2 * phase : 2 - 2 * phase;
            break;
        case Shape::HalfSine:
            normalized = phase < .5 ? std::sin(2 * pi * phase) : 0;
            break;
        case Shape::PositiveSine:
            normalized = std::sin(pi * phase);
            break;
        case Shape::Sawtooth:
            normalized = phase;
            break;
        case Shape::Arbitrary: break; // Refused above.
        }
        const long value = std::lround(codes.minimum + span * normalized);
        samples[i] = value < codes.minimum   ? codes.minimum
                     : value > codes.maximum ? codes.maximum
                                                    : value;
    }
    return true;
}
bool OutputPlan::calculate(const Settings &settings, uint32_t clock, OutputPlan &output, unsigned points) {
    if (!clock || !settings.valid()) return false;
    OutputPlan candidate;
    if (settings.arbitrary()) { // One DAC update a sample, at the waveform's own rate: no table to fit.
        if (!ArbitraryLimits::points(points) || !ArbitraryLimits::rate(settings.arbitrarySamplesPerSecond, clock) ||
            !TimerPlan::calculate(clock, {settings.arbitrarySamplesPerSecond, 1}, candidate.timer, Dac::minimumDivider(clock)))
            return false;
        candidate.count = uint16_t(points);
        uint64_t numerator = candidate.timer.actualHz.numerator, denominator = uint64_t(candidate.timer.actualHz.denominator) * points;
        const uint64_t reduced = std::gcd(numerator, denominator);
        candidate.actualHz = {numerator / reduced, denominator / reduced};
        output = candidate;
        return true;
    }
    if (settings.frequencyHz.value()*1000>DacCapabilities::maximumMillihertz(settings.shape,settings.level,clock))
        return false;
    candidate.pwm = Levels::pwm(settings);
    if (candidate.pwm) {
        if (!TimerPlan::calculate(clock, settings.frequencyHz, candidate.timer, 2))
            return false;
        const uint32_t period = candidate.timer.reload + 1U;
        candidate.compare = (uint64_t(period) * settings.dutyPermille + 500) / 1000;
        if (settings.dutyPermille > 0 && settings.dutyPermille < 1000 &&
            (!candidate.compare || candidate.compare == period))
            return false;
        candidate.actualHz = {candidate.timer.actualHz.numerator, candidate.timer.actualHz.denominator};
    } else {
        candidate.count=dacPoints(settings,clock);
        if (!candidate.count) return false;
        const unsigned high=(unsigned(candidate.count)*settings.dutyPermille+500)/1000;
        if (settings.shape==Shape::Square && settings.dutyPermille && settings.dutyPermille<1000 &&
            (!high || high==candidate.count)) return false;
        Rational rate;
        if (!updateRate(settings.frequencyHz, candidate.count, rate) ||
            !TimerPlan::calculate(clock, rate, candidate.timer, Dac::minimumDivider(clock)))
            return false;
        uint64_t numerator = candidate.timer.actualHz.numerator;
        uint64_t denominator = uint64_t(candidate.timer.actualHz.denominator) * candidate.count;
        const uint64_t reduced = std::gcd(numerator, denominator);
        candidate.actualHz = {numerator / reduced, denominator / reduced};
    }
    output = candidate;
    return true;
}
} // namespace openosc::generator
