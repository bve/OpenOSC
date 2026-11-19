#pragma once
#include "openosc/platform/timer_plan.hpp"
#include "openosc/settings/run_mode.hpp"
#include <cstddef>

namespace openosc::generator {
enum class Shape : uint8_t { Sine, Square, Triangle, HalfSine, PositiveSine, Sawtooth };
enum class Level : uint8_t { Dedicated, Low, High, Custom };
// How a Dedicated square leaves the ZT703S: Factory as GEN 703S apply 2406E65C (DAC samples 0/4000
// below 1 kHz, TIM13 PWM from 1 kHz), or Pwm at every frequency (no DAC stream, exact duty).
enum class SquareDrive : uint8_t { Factory, Pwm };
struct Settings {
    Shape shape = Shape::Sine;
    Level level = Level::Dedicated;
    SquareDrive squareDrive = SquareDrive::Factory;
    Rational frequencyHz{1000, 1};
    uint16_t minimumCode = 1000, maximumCode = 3000, dutyPermille = 500;
    uint16_t amplitudeMillivolts = 3000; // ZT706S module only (factory default 3.0 V); ZT703S levels use codes.
    bool valid() const;
    bool same(const Settings &other) const {
        return shape == other.shape && level == other.level && squareDrive == other.squareDrive && minimumCode == other.minimumCode && maximumCode == other.maximumCode &&
            dutyPermille == other.dutyPermille && amplitudeMillivolts == other.amplitudeMillivolts && uint64_t(frequencyHz.numerator) * other.frequencyHz.denominator ==
                uint64_t(other.frequencyHz.numerator) * frequencyHz.denominator;
    }
};
// The ZT703S calibration reference (GitHub #32): the factory main OUTPUT's selector 0, a TIM13 PWM
// square of 1 kHz at 50 % on the jack (controller 0804EE6C stage 0, PSC core_MHz-1, ARR 999, CCR 500;
// zt_fw docs/subsystems/main_dac_output.md). The same setting is the editor's `Reference 1kHz` preset.
// PWM always (no DMA): its plan comes from the timer clock that runs, not the factory's core MHz.
inline Settings referenceSettings() {
    Settings s;
    s.shape = Shape::Square; s.level = Level::Dedicated; s.squareDrive = SquareDrive::Pwm;
    s.dutyPermille = 500; s.frequencyHz = {1000, 1};
    return s;
}
// When the ZT703S calibration reference may drive the jack (GitHub #32), decided from the instrument's
// state each pass (DataAccess::referenceAllowed). As the factory main firmware: in Scope only (its DMM
// start never runs the output wrapper); never while the instrument stops (shutdown), across a clock
// change, during a zero calibration (the inputs quiet) or while the DMM serial output has the jack.
struct ReferencePolicy {
    struct Inputs { bool initialized, active, stopping, clockSuspended, scope, zeroCalibrating, serialOutput; };
    static constexpr bool allowed(const Inputs &in) {
        return in.initialized && in.active && !in.stopping && !in.clockSuspended && in.scope && !in.zeroCalibrating && !in.serialOutput;
    }
};
struct CodeRange { uint16_t minimum, maximum; };
// ZT703S level profiles: the DAC codes of the factory firmware's two generator paths (zt_fw
// docs/subsystems/generator.md, main_dac_output.md). Dedicated: GEN 703S V1.6.27 builders (sine
// sin*2000+2000 below 1 kHz, sin*1500+2100 from 1 kHz; the others 0..4000; square in kHz on TIM13
// PWM). Low/High: main's OUTPUT level 1/0 (sine 800/2000 offset and scale, square 1500/4000 after
// its duty pass, triangle steps 3/8 to 1500/4000); OpenOSC applies them to all six shapes. Custom:
// the user's codes. Nothing here is calibrated: the factory has no generator calibration (its
// calibration record 0801C000 holds scope offsets, screen gains and the interleave correction).
class Levels {
public:
    static CodeRange codes(const Settings &settings);
    // GEN 703S's kHz units (frequency controller 2406B808: 999 Hz steps up to 1 kHz): the sine's
    // 600..3600 and the factory square's PWM start here.
    static constexpr double KilohertzFrom = 1000;
    static bool kilohertz(const Settings &settings) { return settings.frequencyHz.value() >= KilohertzFrom; }
    // TIM13 PWM on PA6 (a logic level, no DAC codes): a Dedicated square from 1 kHz (Factory) or at
    // every frequency (Pwm). Every other output is DAC samples. OutputPlan, the driver and the UI
    // text all follow this one decision.
    static bool pwm(const Settings &settings) {
        return settings.shape == Shape::Square && settings.level == Level::Dedicated &&
               (settings.squareDrive == SquareDrive::Pwm || kilohertz(settings));
    }
    static constexpr const char *DriveNames[] = {"Factory", "PWM"};
    // The factory's fixed caption of a profile, volts as text ("2.5": GEN 703S "VPP : 2.5V" and
    // main OUTPUT level 0; "1.0": level 1; nullptr: Custom). A UI text, never a measured amplitude.
    static const char *label(Level level);
    // The level as it is applied, for the UI: "DAC 600-3600, factory label 2.5V", "PWM logic level,
    // factory label 2.5V", "Custom DAC 123-2048".
    static void describe(const Settings &settings, char *output, size_t capacity);
    static constexpr const char *Names[] = {"Dedicated", "Low", "High", "Custom"};
};
inline constexpr const char *ShapeNames[] = {"Sine", "Square", "Triangle", "Half sine", "Positive sine", "Sawtooth"};
// ZT703S: DAC2 + BDMA + TIM6 (TIM13 PWM for the dedicated square) on PA6. Frequencies in millihertz.
struct DacCapabilities {
    static constexpr bool Levels = true, Amplitude = false;
    static constexpr uint64_t MinimumMillihertz = 1;
    static constexpr uint16_t MinimumDutyPermille = 0, MaximumDutyPermille = 1000, DutyStepPermille = 5, AmplitudeStepMillivolts = 0;
    // The DAC stream: TIM6 TRGO -> DAC2 -> DMAMUX2 -> BDMA2 from SRAM4, one halfword per update (only
    // BDMA2 reaches DAC2 on PA6; it has no FIFO or burst). Measured on a ZT703S (issue #23,
    // build/reviews/issue23-200mhz-display/hardware/dac-rate-sweep.json, dac-period-sweep.json,
    // candidate-280.json): with a 200 MHz timer clock updates every 25, 24, 23 and 22 ticks streamed
    // through 4 s of polls, every 21 and 20 ticks (9.5, 10 MS/s) underran (DAC2 SR DMAUDR1); with
    // 280 MHz every 28 ticks (10 MS/s) streams. A short observation, not a silicon limit: an update
    // takes at least MinimumTimerTicks (25, kept as margin over 22) and the rate stays at most
    // MaximumSamplesPerSecond (nothing faster was measured). TIM6 runs at the core clock on every run
    // mode (APB1 = HCLK/2, timer x2).
    static constexpr uint32_t MaximumSamplesPerSecond = 10000000, MinimumTimerTicks = 25;
    static constexpr unsigned MinimumPoints = 28, MaximumPoints = 1024;
    // The fewest timer ticks per DAC update at `timerHz`.
    static constexpr uint32_t minimumDivider(uint32_t timerHz) {
        const uint32_t rate = uint32_t((uint64_t(timerHz) + MaximumSamplesPerSecond - 1) / MaximumSamplesPerSecond);
        return rate > MinimumTimerTicks ? rate : MinimumTimerTicks;
    }
    // The highest frequency of a shape and level with the DAC timer at `timerHz`: the shape's own
    // maximum, and for a DAC output at most MinimumPoints samples per period at the fastest update
    // (a Dedicated square is TIM13 PWM from 1 kHz: not limited by the stream). `timerHz` 0: the highest
    // of any run mode (what a stored setting may hold; the profile running decides whether it plays).
    static uint64_t maximumMillihertz(Shape shape, Level level = Level::Dedicated, uint32_t timerHz = 0) {
        if (!timerHz) { // The highest of any run mode.
            uint64_t highest = 0;
            for (const auto &mode : RunModes) {
                const uint64_t top = maximumMillihertz(shape, level, mode.coreHz);
                if (top > highest) highest = top;
            }
            return highest;
        }
        constexpr uint64_t maxima[] = {344000000, 1000000000, 100000000, 129000000, 294000000, 100000000};
        if (unsigned(shape) >= 6) return 0;
        // A square on the DAC: 100 kHz. A Dedicated square is PWM from 1 kHz up (either drive): no stream.
        if (shape == Shape::Square && level == Level::Dedicated) return maxima[1];
        const uint64_t stream = uint64_t(timerHz) * 1000 / (uint64_t(minimumDivider(timerHz)) * MinimumPoints);
        const uint64_t own = shape == Shape::Square ? 100000000 : maxima[unsigned(shape)];
        return own < stream ? own : stream;
    }
    static bool duty(Shape shape) { return shape == Shape::Square; }
    static uint16_t amplitudeLimitMillivolts(Shape, uint64_t) { return 5000; } // Not used: the module's field is kept as stored.
    // Frequency within the shape and level at `timerHz` (0: any run mode; the timer plan checks what
    // it can represent).
    static bool accepts(const Settings &s, uint32_t timerHz = 0) {
        const double millihertz = s.frequencyHz.value() * 1000;
        return s.valid() && millihertz >= MinimumMillihertz && millihertz <= double(maximumMillihertz(s.shape, s.level, timerHz));
    }
};
class WaveTable {
public:
    static bool build(const Settings &settings, uint16_t *samples, size_t count);
};
struct OutputPlan {
    bool pwm = false;
    uint16_t count = 0;
    uint32_t compare = 0;
    TimerPlan timer;
    WideRational actualHz;
    static bool calculate(const Settings &settings, uint32_t timerClockHz, OutputPlan &output);
};
// Why the output stopped by itself (Output::service, a start after a clock change): Underrun DAC2
// DMAUDR1, Transfer a BDMA2 transfer error, Limit the setting has no plan at the clock that runs.
enum class OutputFault : uint8_t { None, Underrun, Transfer, Limit };
inline const char *faultText(OutputFault fault) {
    switch (fault) {
    case OutputFault::Underrun: return "Generator stopped: DAC underrun";
    case OutputFault::Transfer: return "Generator stopped: DMA transfer error";
    case OutputFault::Limit: return "Generator off: frequency above this clock's DAC limit";
    default: return "";
    }
}
struct Snapshot {
    Settings requested;
    OutputPlan applied;
    DataStatus status = DataStatus::NoData;
    bool enabled = false;
    uint32_t timerHz = 0;  // The DAC timer clock the limits are computed at (0: no DAC, the ZT706S module).
    uint32_t faults = 0;   // Stops the output made by itself so far (each one reported once).
    OutputFault fault = OutputFault::None; // The last of them.
};
} // namespace openosc::generator
