#pragma once
#include "openosc/settings/run_mode.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/acquisition/sample_plan.hpp"
#include "openosc/instrument/mode.hpp"
#include "openosc/measurement/meter_function.hpp"
#include "openosc/settings/instrument_preferences.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/platform/idle_policy.hpp"
#include "openosc/power/battery.hpp"
#include "openosc/settings/tooltips.hpp"
#include "openosc/settings/bode_preferences.hpp"
#include <cstdint>

namespace openosc {
struct Preferences {
    uint32_t brightness = 65, mode = 0;
    uint64_t timebaseNs = 1000000;
    uint32_t millivolts[2] = {500, 500}, meterFunction = 0;
    uint32_t useBuiltinTheme = 0;
    uint8_t showFps = 1; // 0 off, 1 FPS, 2 sample rate (scope).
    // Settings > Display > Tooltips (GitHub #108): the kinds hidden, a bit each (Tooltips); 0 (older
    // files) shows all. In showFps's word: the layout keeps its size.
    uint8_t tooltipsHidden = 0;
    uint8_t batteryDisplay = 0; // 0 icon, 1 volts, 2 percent.
    // The battery display trim (power::BatteryGauge::trim, GitHub #107), per instrument: gain in 0.1 %
    // steps and offset in mV; zero (older files) shows the factory scale. One word with batteryDisplay.
    int8_t batteryGainPermille = 0;
    int16_t batteryOffsetMv = 0;
    // Bytes beside batteryOffsetMv, with bode after them: the space four 32-bit words took (sizeof checked below).
    uint8_t panReverse = 0;                   // Left/Right pan: 0 moves the view, 1 the waveform.
    uint8_t clockIndicator = 0;               // Scope top bar L/M/H run profile glyph: 0 off (older files), 1 on.
    uint8_t holdTime = 3;                     // REL power-off hold: HoldTimes index; 0 is a short press, no hold.
    static constexpr uint32_t HoldTimes[] = {0, 1000, 1500, 2000};
    bool shortPowerOff() const { return holdTime == 0; } // REL's short press powers off; its own function moves to a 1 s hold.
    uint32_t holdMs() const { return HoldTimes[holdTime < 4 ? holdTime : 3]; }
    // Settings > Files & USB > USB controller: 1 enabled (USB data where the clock allows it, 200 MHz),
    // 0 disabled (the OTG controller stays off; Recovery's USB disk is not affected). Older files: enabled.
    uint8_t usbController = 1;
    BodePreferences bode; // Analyze > Bode setup (GitHub #124), in the file's scope object.
    uint32_t generatorShape = 0, generatorDutyPermille = 500;
    uint64_t generatorMillihertz = 1000000; // uint64: the ZT706S module reaches 10 MHz = 1e10 mHz.
    uint32_t generatorLevel = 0, generatorMinimumCode = 1000, generatorMaximumCode = 3000;
    uint32_t generatorSquareDrive = 0; // generator::SquareDrive (ZT703S Dedicated square): 0 factory, 1 PWM.
    // Startup output (GeneratorStartup): 0 off after power-up, 1 restore the last confirmed state;
    // generatorLastOutput is that state (1 on).
    uint32_t generatorStartup = 0, generatorLastOutput = 0;
    GeneratorArrangement generatorArrangement; // The main GEN screen's blocks (default: frequency, preview, buttons).
    uint32_t generatorAmplitudeMillivolts = 3000; // ZT706S module; ZT703S keeps level/codes.
    ScopePreferences scope;
    MeterPreferences meter;
    IdleSettings idle;
    bool valid() const {
        scope::SamplePlan sampling;
        return brightness >= 10 && brightness <= 100 && mode < InstrumentModeCount && timebaseNs >= 10 &&
               timebaseNs <= 20000000000ULL && scope::VoltageRanges::contains(millivolts[0]) &&
               scope::VoltageRanges::contains(millivolts[1]) && meterFunction < MeterFunctionCount &&
               useBuiltinTheme <= 1 && showFps <= 2 && Tooltips::valid(tooltipsHidden) && batteryDisplay < 3 && power::BatteryGauge::trimValid(batteryGainPermille, batteryOffsetMv) && panReverse < 2 && clockIndicator <= 1 && holdTime < 4 && usbController <= 1 && bode.valid() &&
               generatorShape < 6 &&
               generatorMillihertz >= generator::Capabilities::MinimumMillihertz &&
               generatorMillihertz <= generator::Capabilities::maximumMillihertz(generator::Shape(generatorShape),generator::Level(generatorLevel)) &&
               generatorDutyPermille >= generator::Capabilities::MinimumDutyPermille &&
               generatorDutyPermille <= generator::Capabilities::MaximumDutyPermille &&
               generatorAmplitudeMillivolts <= generator::Capabilities::amplitudeLimitMillivolts(generator::Shape(generatorShape), generatorMillihertz) &&
               (!generator::Capabilities::AmplitudeStepMillivolts || !(generatorAmplitudeMillivolts % (generator::Capabilities::AmplitudeStepMillivolts ? generator::Capabilities::AmplitudeStepMillivolts : 1))) &&
               generatorLevel<4 && generatorSquareDrive<2 && generatorStartup<2 && generatorLastOutput<2 && generatorArrangement.valid() && generatorMinimumCode<=generatorMaximumCode &&
               generatorMaximumCode<=4095 && scope.valid() && meter.valid() && idle.valid() &&
               scope::SamplePlan::calculate(timebaseNs,
                    scope::RecordBudget::perChannel(scope.recordPoints,scope::RecordBudget::channels(scope.enabled[0],scope.enabled[1])),
                    RunModes[scope.runMode].maximumSampleRate(),scope::RecordBudget::Maximum,sampling);
    }
};
#if defined(__arm__)
// Several stacks hold a Preferences (the load's candidate and its parse's, the defaults): it must not grow.
static_assert(sizeof(Preferences) <= 632, "Preferences grew: it sits on the stack");
#endif
} // namespace openosc
