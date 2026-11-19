#pragma once
#include "openosc/settings/run_mode.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/acquisition/sample_plan.hpp"
#include "openosc/instrument/mode.hpp"
#include "openosc/measurement/meter_function.hpp"
#include "openosc/settings/instrument_preferences.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/platform/idle_policy.hpp"
#include <cstdint>

namespace openosc {
struct Preferences {
    uint32_t brightness = 65, mode = 0;
    uint64_t timebaseNs = 1000000;
    uint32_t millivolts[2] = {500, 500}, meterFunction = 0;
    uint32_t useBuiltinTheme = 0;
    uint32_t showFps = 1, batteryDisplay = 0; // showFps: 0 off, 1 FPS, 2 sample rate (scope). batteryDisplay: 0 icon, 1 volts, 2 percent.
    uint32_t panReverse = 0;                  // Left/Right pan: 0 moves the view, 1 the waveform.
    uint32_t holdTime = 2;                    // REL power-off hold: HoldTimes index.
    static constexpr uint32_t HoldTimes[] = {1000, 1500, 2000};
    uint32_t holdMs() const { return HoldTimes[holdTime < 3 ? holdTime : 2]; }
    // Settings > Files & USB > USB controller: 1 enabled (USB data where the clock allows it, 200 MHz),
    // 0 disabled (the OTG controller stays off; Recovery's USB disk is not affected). Older files: enabled.
    uint32_t usbController = 1;
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
               useBuiltinTheme <= 1 && showFps <= 2 && batteryDisplay < 3 && panReverse < 2 && holdTime < 3 && usbController <= 1 &&
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
} // namespace openosc
