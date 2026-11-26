#pragma once
#include "openosc/generator/waveform.hpp"
#include "openosc/generator/frame_codec.hpp"
#include <cstdint>

namespace openosc::generator {
// The ZT706S external generator module's waveform, as the factory GEN 706S V1.6.31 computes it
// (zt_fw docs/subsystems/generator.md, generator_controls.md; addresses below are 706S GEN). The
// module plays N one-byte samples, each held `divisor` ticks of a clock the factory takes as 200 MHz
// (a software constant: the module's real clock and output voltage are not established). OpenOSC
// keeps the factory's amplitude numbers (limit, correction, code) exactly; the waveform and its timing
// are exact where the factory's are approximate or unsafe (each noted at its function):
//  - amplitude 0 V is a zero waveform (the factory wraps it to full scale);
//  - the limit follows shape and frequency both ways (the factory only lowers it), and the
//    correction is computed from the amplitude already clamped to it;
//  - samples: exact pi and 2*pi phases, full scale reached (the factory steps 6.28/N and 3.14/N from
//    code>>1 and accumulates float steps); the square has round(N*duty) high samples, 100 % all high
//    (the factory's "i <= threshold" leaves a zero sample at 100 % and none high at 1 % of 100); the
//    duty in permille as set (the factory's whole percents are a subset);
//  - timing: every legal sample count 20..255 (even for a triangle, one that holds the duty for a
//    square) with floor and ceil divisors, compared by the exact rational error (the factory starts at
//    the amplitude code, floors only, truncates the error to an integer, stops below N=100 and sends
//    N=0/divisor=0 below code 20).
class ModuleWave {
public:
    static constexpr uint32_t ClockHz = 200000000;      // Factory constant, nominal.
    static constexpr unsigned MinimumSamples = 20, MaximumSamples = SerialFrameCodec::MaximumSamples;
    static constexpr uint16_t MaximumMillivolts = 5000;  // Factory amplitude UI 0..5 V, 0.1 V steps.
    static constexpr uint8_t MinimumDuty = 1, MaximumDuty = 100; // Percent, the factory's 1..100 (100: constant high).
    struct Timing { uint32_t divisor = 0; unsigned count = 0; };
    struct Frame {
        uint8_t bytes[SerialFrameCodec::Bytes]{};
        uint8_t acknowledge = 0;   // Folded checksum: (low + high) of the frame's sum, as the factory main.
        Timing timing;
        uint16_t code = 0;         // Full-scale sample value (0: a zero waveform).
        WideRational actualHz;     // Nominal: ClockHz / (count * divisor).
    };
    // Upper amplitude for a shape and frequency, millivolts (2406E008's caps: 5, 4, 3.5, 3 V).
    static uint16_t limitMillivolts(Shape shape, uint64_t millihertz);
    // Pre-correction in code units for the clamped amplitude (2406E008, float32 then x1.2 in double).
    static float correction(Shape shape, uint32_t hertz, float volts);
    // Full-scale code for the clamped amplitude and its correction (2407178C).
    static uint16_t code(float volts, float correction);
    // A square's high samples of `count` at `permille`: round(count*permille/1000); and whether that is
    // the waveform asked for: 0 none and 1000 all high (the endpoints), between them neither none nor all.
    static unsigned highSamples(unsigned count, unsigned permille) { return (count * (permille > 1000 ? 1000 : permille) + 500) / 1000; }
    static bool holds(unsigned count, unsigned permille) {
        const unsigned high = highSamples(count, permille);
        return !permille ? !high : permille >= 1000 ? high == count : high && high < count;
    }
    // Sample count and divisor nearest `millihertz` (exact rational error) among the counts the shape
    // can use (even for a triangle, holding the duty for a square); equal errors: the better shape (the
    // square's nearer duty, a sine family's count by 4, then more samples). count 0: none can.
    static Timing timing(uint64_t millihertz, Shape shape, unsigned dutyPermille = 500);
    // `count` samples of `shape` from 0 to `code`; the square's duty in permille (low run first).
    static void samples(Shape shape, unsigned count, uint16_t code, unsigned dutyPermille, uint8_t *output);
    // The frame for `settings` (OFF: `enabled` false, a zero waveform at 1 kHz as the factory's OFF,
    // whatever the shape). Arbitrary has its own frame (buildArbitrary): refused here when enabled.
    static bool build(const Settings &settings, bool enabled, Frame &frame);
    // The divisor nearest `samplesPerSecond` (ModuleArbitraryLimits): floor or ceil of ClockHz / rate,
    // whichever gives the smaller |ClockHz / divisor - rate| (compared exactly; equal: the floor, the
    // higher rate). 0 for a rate outside the limits.
    static uint32_t arbitraryDivisor(uint32_t samplesPerSecond);
    // The Arbitrary waveform's frame: `points` codes (0..4095, ModuleArbitraryLimits) each sent as
    // ModuleArbitraryLimits::sample, all of them (never decimated or cut), held arbitraryDivisor ticks.
    // actualHz: the nominal repetition, ClockHz / (points * divisor). False: points, a code or the rate
    // outside the limits (nothing built).
    static bool buildArbitrary(const uint16_t *codes, unsigned points, uint32_t samplesPerSecond, Frame &frame);
};
static_assert(ModuleArbitraryLimits::ClockHz == ModuleWave::ClockHz && ModuleArbitraryLimits::MaximumPoints == ModuleWave::MaximumSamples &&
              ModuleArbitraryLimits::MinimumPoints == ModuleWave::MinimumSamples, "The AWG frame is the module's own frame");
} // namespace openosc::generator
