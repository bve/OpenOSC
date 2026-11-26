#include "openosc/generator/module_wave.hpp"
#include "openosc/generator/frequency.hpp"
#include <cmath>
#include <cstdlib>
#include <initializer_list>

// float32 steps as the factory's FPU code (no fused multiply-add: its rounding differs).
#define OPENOSC_FACTORY_FLOAT __attribute__((optimize("fp-contract=off")))

namespace openosc::generator {
namespace {
// VCVT to unsigned, round toward zero, saturating (NaN and negatives give 0).
uint32_t unsignedOf(double value) {
    if (!(value > 0)) return 0;
    return value >= 4294967295.0 ? 0xffffffffU : uint32_t(value);
}
float f32(double value) { return float(value); }
} // namespace

uint16_t ModuleWave::limitMillivolts(Shape shape, uint64_t millihertz) {
    const uint64_t hertz = millihertz / 1000;
    switch (shape) {
    case Shape::Sine:
        return hertz < 1000000 ? 5000 : hertz < 7000000 ? 4000 : hertz < 10000000 ? 3500 : 3000;
    case Shape::Triangle: case Shape::HalfSine: case Shape::PositiveSine:
        return hertz < 1000000 ? 5000 : 4000;
    case Shape::Sawtooth: return 4000;
    case Shape::Square: case Shape::Arbitrary: break; // Arbitrary: kept for the other shapes (its codes set its level).
    }
    return 5000;
}
OPENOSC_FACTORY_FLOAT float ModuleWave::correction(Shape shape, uint32_t hertz, float volts) {
    float raw = 0;
    switch (shape) {
    case Shape::Sine:
        if (hertz < 1000000) break;
        if (hertz < 4000000) raw = float((hertz / 166667) >> 2) * volts;      // Integer steps.
        else if (hertz < 5000000) raw = float((hertz / 133333) >> 2) * volts;
        else if (hertz < 6000000) raw = volts * 10.0f;
        else if (hertz < 7000000) raw = volts * 15.0f;
        else if (hertz < 8000000) raw = volts * 15.714286f;
        else if (hertz < 9000000) raw = volts * 20.0f;
        else if (hertz < 10000000) raw = volts * 24.285715f;
        else raw = volts * 40.0f;
        break;
    case Shape::Square: if (hertz >= 1000000) raw = volts * 3.0f; break;
    case Shape::Triangle: case Shape::PositiveSine: if (hertz >= 1000000) raw = volts * 6.25f; break;
    case Shape::HalfSine: if (hertz >= 1000000) raw = volts * 5.0f; break;
    case Shape::Sawtooth: raw = volts * 5.0f; break;
    case Shape::Arbitrary: break;
    }
    return f32(double(raw) * 1.2);
}
OPENOSC_FACTORY_FLOAT uint16_t ModuleWave::code(float volts, float correction) {
    if (volts > 5.0f) volts = 5.0f;
    const uint32_t scaled = unsignedOf(volts * 51.0f);
    uint16_t result = uint16_t(scaled);
    const auto corrected = [&](float coefficient) {
        const uint16_t reduced = uint16_t(unsignedOf(float(uint16_t(scaled)) - volts * coefficient));
        return uint16_t(unsignedOf(float(reduced) + correction));
    };
    if (volts >= 4.0f && volts < 5.0f) result = corrected(12.0f);
    else if (volts >= 3.0f && volts < 4.0f) result = corrected(13.3f);
    else if (volts >= 2.0f && volts < 3.0f) result = corrected(13.0f);
    else if (volts >= 1.0f && volts < 2.0f) result = corrected(15.0f);
    else if (volts >= 0.1f && volts < 1.0f) result = uint16_t(unsignedOf(volts * 42.5f));
    else if (correction == 0.0f) result = uint16_t(result - 57); // 5 V: 198. (0 V wraps: build() never asks.)
    return result > 255 ? 255 : result;
}
namespace {
// |ClockHz/(count*divisor) - millihertz/1000| as numerator/denominator, both exact. With a floor or
// ceil divisor the numerator is at most millihertz*count and the denominator 1000*count*divisor at
// most 1000*(ClockHz*1000/millihertz + count): the cross products stay below 7.1e17 in 64 bits.
struct Error {
    uint64_t numerator, denominator;
    bool below(const Error &b) const { return numerator * b.denominator < b.numerator * denominator; }
    bool same(const Error &b) const { return numerator * b.denominator == b.numerator * denominator; }
};
Error error(uint64_t millihertz, unsigned count, uint64_t divisor) {
    const uint64_t clock = uint64_t(ModuleWave::ClockHz) * 1000, played = millihertz * count * divisor;
    return {clock > played ? clock - played : played - clock, uint64_t(1000) * count * divisor};
}
// At equal frequency, the better shape: the square's duty nearer (|high/count - duty/100|, compared
// exactly), a sine family's count with samples on its peaks, then more samples.
bool finer(Shape shape, unsigned duty, unsigned a, unsigned b) {
    if (shape == Shape::Square) {
        const uint64_t ea = uint64_t(std::abs(int(1000 * ModuleWave::highSamples(a, duty)) - int(duty * a))) * b,
                       eb = uint64_t(std::abs(int(1000 * ModuleWave::highSamples(b, duty)) - int(duty * b))) * a;
        if (ea != eb) return ea < eb;
    }
    const unsigned peaks = shape == Shape::Sine || shape == Shape::HalfSine ? 4 : shape == Shape::PositiveSine ? 2 : 1;
    if (!(a % peaks) != !(b % peaks)) return !(a % peaks);
    return a > b;
}
} // namespace
ModuleWave::Timing ModuleWave::timing(uint64_t millihertz, Shape shape, unsigned duty) {
    Timing best;
    if (!millihertz || millihertz > uint64_t(ClockHz) * 1000) return best;
    Error bestError{0, 1};
    for (unsigned count = MinimumSamples; count <= MaximumSamples; ++count) {
        if (shape == Shape::Triangle && (count & 1)) continue;
        if (shape == Shape::Square && !holds(count, duty)) continue;
        const uint64_t floor = uint64_t(ClockHz) * 1000 / (millihertz * count);
        for (const uint64_t divisor : {floor, floor + 1}) {
            if (!divisor || divisor > 0xffffffffULL) continue;
            const Error e = error(millihertz, count, divisor);
            if (!best.count || e.below(bestError) || (e.same(bestError) && finer(shape, duty, count, best.count))) {
                best = {uint32_t(divisor), count}; bestError = e;
            }
        }
    }
    return best;
}
void ModuleWave::samples(Shape shape, unsigned count, uint16_t code, unsigned duty, uint8_t *output) {
    constexpr double pi = 3.14159265358979323846;
    const unsigned high = highSamples(count, duty);
    for (unsigned i = 0; i < count; ++i) {
        double value = 0;
        switch (shape) {
        case Shape::Sine: value = code * (1 + std::sin(2 * pi * i / count)) / 2; break;
        case Shape::Square: value = i >= count - high ? code : 0; break;
        case Shape::Triangle: value = double(code) * 2 * (i <= count / 2 ? i : count - i) / count; break;
        case Shape::HalfSine: value = code * std::sin(2 * pi * i / count); break; // Negative half: 0 below.
        case Shape::PositiveSine: value = code * std::sin(pi * i / count); break;
        case Shape::Sawtooth: value = count > 1 ? double(code) * i / (count - 1) : 0; break;
        case Shape::Arbitrary: break;
        }
        const long rounded = std::lround(value);
        output[i] = uint8_t(rounded < 0 ? 0 : rounded > code ? code : rounded);
    }
}
namespace {
// The frame's encoding, its acknowledge and nominal frequency, from the samples at frame.bytes + 14
// (SerialFrameCodec::encode moves them in place) and frame.timing.
bool seal(ModuleWave::Frame &frame, const uint8_t *samples) {
    if (!SerialFrameCodec::encode(samples, frame.timing.count, frame.timing.divisor, frame.bytes, sizeof frame.bytes))
        return false;
    const unsigned checksum = 14 + frame.timing.count;
    frame.acknowledge = uint8_t(frame.bytes[checksum] + frame.bytes[checksum + 1]);
    frame.actualHz = {ModuleWave::ClockHz, uint64_t(frame.timing.count) * frame.timing.divisor};
    return true;
}
} // namespace
uint32_t ModuleWave::arbitraryDivisor(uint32_t rate) {
    if (!ModuleArbitraryLimits::rate(rate)) return 0;
    const uint64_t floor = ClockHz / rate, ceil = floor + 1;
    // |ClockHz/d - rate| = |ClockHz - rate*d| / d: both remainders are below `rate`, so the cross
    // products stay below 2^56.
    const uint64_t below = ClockHz - rate * floor, above = rate * ceil - ClockHz;
    return uint32_t(above * floor < below * ceil ? ceil : floor);
}
bool ModuleWave::buildArbitrary(const uint16_t *codes, unsigned points, uint32_t rate, Frame &frame) {
    frame = {};
    if (!codes || !ModuleArbitraryLimits::points(points)) return false;
    frame.timing = {arbitraryDivisor(rate), points};
    if (!frame.timing.divisor) return false;
    uint8_t *samples = frame.bytes + 14; // Built in their place in the frame: one buffer on the stack.
    for (unsigned i = 0; i < points; ++i) {
        if (codes[i] > ModuleArbitraryLimits::MaximumCode) { frame = {}; return false; }
        samples[i] = ModuleArbitraryLimits::sample(codes[i]);
    }
    frame.code = 255; // The byte scale: the codes set the level.
    if (seal(frame, samples)) return true;
    frame = {};
    return false;
}
bool ModuleWave::build(const Settings &settings, bool enabled, Frame &frame) {
    frame = {};
    const uint64_t millihertz = Frequency::millihertz(settings.frequencyHz);
    // OFF is the same zero waveform for every shape (Arbitrary too: the boot's OFF, a saved AWG).
    if (!settings.valid() || (enabled && (settings.arbitrary() || !millihertz))) return false;
    uint8_t wave[MaximumSamples];
    const Shape shape = settings.shape;
    uint64_t target = millihertz;
    if (enabled && settings.amplitudeMillivolts) {
        const uint16_t limit = limitMillivolts(shape, millihertz);
        const float volts = float(settings.amplitudeMillivolts < limit ? settings.amplitudeMillivolts : limit) / 1000.0f;
        const uint32_t hertz = uint32_t(millihertz / 1000);
        frame.code = code(volts, correction(shape, hertz, volts));
    } else target = 1000000; // A zero waveform at 1 kHz (factory OFF: shape 100 at 1000 Hz).
    // The duty as set, in permille: never cut to whole percents (50.5 % is not 50 %).
    unsigned duty = settings.dutyPermille;
    duty = duty < MinimumDuty * 10U ? MinimumDuty * 10U : duty > MaximumDuty * 10U ? MaximumDuty * 10U : duty;
    // The count holds the shape (a zero waveform any count): never a pulse turned into none or DC.
    frame.timing = timing(target, frame.code ? shape : Shape::Sine, duty);
    if (!frame.timing.count || !frame.timing.divisor) return false;
    if (frame.code) samples(shape, frame.timing.count, frame.code, duty, wave);
    else for (unsigned i = 0; i < frame.timing.count; ++i) wave[i] = 0;
    return seal(frame, wave);
}
} // namespace openosc::generator
