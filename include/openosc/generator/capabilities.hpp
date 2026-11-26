#pragma once
#include "openosc/board/current.hpp"
#include "openosc/generator/module_wave.hpp"
#include "openosc/generator/frequency.hpp"
#include <cstdio>
#include <type_traits>

namespace openosc::generator {
// ZT706S: the external module on USART3 (ModuleWave). Limits of the factory GEN 706S UI: frequency
// clamp 24071D4C (1 Hz; sine 10 MHz, square/triangle/half/positive 2 MHz, saw 200 kHz), duty 2407105C
// (1..100 %, here in permille as for the DAC; 100 % a constant high), amplitude 0..5 V on the 0.1 V
// grid with the limit of 2406E008.
// Nominal: the module's real clock and output have not been measured.
struct ModuleCapabilities {
    static constexpr bool Levels = false, Amplitude = true;
    static constexpr uint64_t MinimumMillihertz = 1000;
    static constexpr uint16_t MinimumDutyPermille = ModuleWave::MinimumDuty * 10, MaximumDutyPermille = ModuleWave::MaximumDuty * 10,
                              DutyStepPermille = 10, AmplitudeStepMillivolts = 100;
    // No DAC stream: the module's limits at any core clock (`timerHz` is the DAC's, never used here).
    // Arbitrary plays at its own sample rate (ArbitraryLimits): the frequency kept for the other shapes
    // meanwhile may be any of theirs (the sine's 10 MHz).
    static uint64_t maximumMillihertz(Shape shape, Level = Level::Dedicated, uint32_t = 0) {
        return shape == Shape::Sine || shape == Shape::Arbitrary ? 10000000000ULL : shape == Shape::Sawtooth ? 200000000ULL : 2000000000ULL;
    }
    static bool duty(Shape shape) { return shape == Shape::Square; }
    static uint16_t amplitudeLimitMillivolts(Shape shape, uint64_t millihertz) { return ModuleWave::limitMillivolts(shape, millihertz); }
    // An Arbitrary waveform's level: its codes, each sent as the module's nearest byte (nominal volts).
    static constexpr const char *ArbitraryLevel = "Codes 8-bit", *ArbitraryLevelDetail = "AWG: codes 0-4095 sent as module bytes 0-255";
    // The highest sine the module plays at `amplitudeMillivolts` as set (its limit for that frequency not
    // below it) and represents exactly: ClockHz/n, n samples of one tick each (ModuleWave::timing finds
    // that very n for it); 0 for an amplitude no frequency takes. 10 MHz at 3 V or less, 9.52 MHz at
    // 3.5 V, 6.90 MHz at 4 V, 995 kHz at 5 V (nominal: the module's clock is the factory's constant).
    static uint64_t sineMaximumMillihertz(uint16_t amplitudeMillivolts) {
        for (unsigned n = ModuleWave::MinimumSamples; n <= ModuleWave::MaximumSamples; ++n) {
            const uint64_t millihertz = uint64_t(ModuleWave::ClockHz) * 1000 / n;
            if (millihertz <= maximumMillihertz(Shape::Sine) && amplitudeMillivolts <= ModuleWave::limitMillivolts(Shape::Sine, millihertz))
                return millihertz;
        }
        return 0;
    }
    // The one validation of a setting (driver, SCPI, settings file): what the module can do as asked,
    // nothing clamped silently. A shape whose limit is below the frequency or amplitude already set
    // is refused (the UI editors clamp first and say so), and a square whose duty no sample count at
    // this frequency holds (1 % needs 50 samples or more). Arbitrary: its rate (the codes are the
    // waveform's; the other shapes' settings are kept as they are).
    static bool accepts(const Settings &s, uint32_t = 0) {
        if (s.arbitrary()) return s.valid() && ArbitraryLimits::rate(s.arbitrarySamplesPerSecond);
        const uint64_t millihertz = Frequency::millihertz(s.frequencyHz);
        return s.valid() && millihertz >= MinimumMillihertz && millihertz <= maximumMillihertz(s.shape) &&
               s.dutyPermille >= MinimumDutyPermille && s.dutyPermille <= MaximumDutyPermille &&
               !(s.amplitudeMillivolts % AmplitudeStepMillivolts) && // The factory's 0.1 V grid, everywhere.
               s.amplitudeMillivolts <= amplitudeLimitMillivolts(s.shape, millihertz) &&
               ModuleWave::timing(millihertz, s.shape, s.dutyPermille).count;
    }
};
using Capabilities = std::conditional_t<board::Current::Features.generator == board::GeneratorDriver::SerialModule,
                                        ModuleCapabilities, DacCapabilities>;
// The value the FREQUENCY editor and the screen's Left/Right steps change, in thousandths (GitHub
// #175): a shape's frequency in mHz, the Arbitrary waveform's sample rate in mS/s (whole S/s when set:
// its frequency is that rate over its points).
struct Tuning {
    static uint64_t value(const Settings &s) {
        return s.arbitrary() ? uint64_t(s.arbitrarySamplesPerSecond) * 1000 : Frequency::millihertz(s.frequencyHz);
    }
    static void set(Settings &s, uint64_t milli) {
        if (s.arbitrary()) s.arbitrarySamplesPerSecond = uint32_t((milli + 500) / 1000);
        else s.frequencyHz = Frequency::rational(milli);
    }
    static uint64_t minimum(const Settings &s) {
        return s.arbitrary() ? uint64_t(ArbitraryLimits::MinimumSamplesPerSecond) * 1000 : Capabilities::MinimumMillihertz;
    }
    static uint64_t maximum(const Settings &s, uint32_t timerHz) {
        return s.arbitrary() ? uint64_t(ArbitraryLimits::maximumSamplesPerSecond(timerHz)) * 1000
                             : Capabilities::maximumMillihertz(s.shape, s.level, timerHz);
    }
};
// What sets the output level, in words for the generator screen: ZT703S its level profile
// (Levels), ZT706S the module's amplitude as set, nominal (its output has not been measured).
struct LevelText {
    static void name(const Settings &s, char *output, size_t capacity) { // "Dedicated", "3.0 V".
        if (s.arbitrary()) std::snprintf(output, capacity, "%s", Capabilities::ArbitraryLevel); // The waveform's own codes.
        else if (Capabilities::Levels) std::snprintf(output, capacity, "%s", Levels::Names[unsigned(s.level) % 4]);
        else std::snprintf(output, capacity, "%u.%u V", s.amplitudeMillivolts / 1000, s.amplitudeMillivolts % 1000 / 100);
    }
    static void describe(const Settings &s, char *output, size_t capacity) {
        if (s.arbitrary()) std::snprintf(output, capacity, "%s", Capabilities::ArbitraryLevelDetail);
        else if (Capabilities::Levels) Levels::describe(s, output, capacity);
        else std::snprintf(output, capacity, "Amplitude %u.%u V, module nominal", s.amplitudeMillivolts / 1000,
                           s.amplitudeMillivolts % 1000 / 100);
    }
};
} // namespace openosc::generator
