#include "openosc/application/controller.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/generator/frequency.hpp"
#include "openosc/generator/frequency_steps.hpp"
#include "openosc/platform/input.hpp"
#include <algorithm>

namespace openosc {
void ApplicationController::generatorKeys(uint32_t keys, uint32_t now) {
    using Caps = generator::Capabilities;
    auto &output = data_.generator();
    if (keys & (Keypad::F1 | Keypad::F3)) { openGeneratorEditor(keys & Keypad::F1); return; }
    if (keys & (Keypad::F4 | Keypad::HoldSave)) view_.focusParameter(3);
    if (keys & (Keypad::F2 | Keypad::Menu)) { openFrequencyEditor(); return; }
    if (keys & (Keypad::HoldSave | Keypad::F4)) {
        if (!requestGeneratorOutput(!output.enabled(), now) && !output.enabled()) view_.notify("Cannot enable output", now);
    }
    auto settings = output.settings();
    uint64_t frequency = prefs_.generatorMillihertz;
    const uint32_t timerHz = output.snapshot().timerHz; // The DAC limits at the clock that runs.
    const uint64_t maximum = Caps::maximumMillihertz(settings.shape, settings.level, timerHz);
    // The decade grid (GitHub #38): reversible steps, 1 kHz -> 2 kHz -> 1 kHz; exact values in FREQUENCY.
    using Steps = generator::FrequencySteps;
    if (keys & Keypad::Right) frequency = Steps::up(frequency, Caps::MinimumMillihertz, maximum);
    if (keys & Keypad::Left) frequency = Steps::down(frequency, Caps::MinimumMillihertz, maximum);
    settings.frequencyHz = generator::Frequency::rational(frequency);
    if (Caps::duty(settings.shape)) {
        const int duty = int(settings.dutyPermille) + ((keys & Keypad::Up) ? 50 : (keys & Keypad::Down) ? -50 : 0);
        settings.dutyPermille = uint16_t(std::clamp<int>(duty, Caps::MinimumDutyPermille, Caps::MaximumDutyPermille));
    } else if (keys & (Keypad::Up | Keypad::Down)) {
        if (Caps::Levels) {
            settings.level = generator::Level((unsigned(settings.level) + ((keys & Keypad::Up) ? 1 : 3)) % 4);
            settings.frequencyHz = generator::Frequency::rational(
                std::min(frequency, Caps::maximumMillihertz(settings.shape, settings.level, timerHz)));
        } else { // The module's amplitude, 0.1 V steps up to the shape's limit at this frequency.
            const int top = Caps::amplitudeLimitMillivolts(settings.shape, frequency);
            const int next = int(settings.amplitudeMillivolts) + ((keys & Keypad::Up) ? 1 : -1) * Caps::AmplitudeStepMillivolts;
            settings.amplitudeMillivolts = uint16_t(std::clamp(next, 0, top));
        }
    }
    if (Caps::Amplitude) settings.amplitudeMillivolts = std::min(settings.amplitudeMillivolts, Caps::amplitudeLimitMillivolts(settings.shape, frequency));
    if (keys & (Keypad::Left | Keypad::Right | Keypad::Up | Keypad::Down)) {
        if (!applyGenerator(settings, now)) view_.notify("Unsupported output settings", now);
    }
}
} // namespace openosc
