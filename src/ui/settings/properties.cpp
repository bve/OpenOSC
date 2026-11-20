#include "openosc/ui/settings/properties.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/platform/idle_policy.hpp"
#include "openosc/settings/run_mode.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/usb/clock_policy.hpp"
#include "openosc/acquisition/interleave.hpp"
#include <cstdio>
#include <cstdlib>

namespace openosc::ui {
namespace {
using Action = SettingsAction;
constexpr uint32_t Backlight[] = {0, 30, 60, 120};
constexpr unsigned BacklightCount = sizeof Backlight / sizeof *Backlight;
constexpr unsigned DimCount = sizeof IdleSettings::DimLevels / sizeof *IdleSettings::DimLevels;
constexpr unsigned OffCount = sizeof IdleSettings::OffChoices / sizeof *IdleSettings::OffChoices;
constexpr const char *Batteries[] = {"Icon", "Volts", "Percent"};
// Preferences::usbController (the index). The data link only runs on ClockPolicy's profile.
constexpr const char *UsbControllers[] = {"Disabled", "Enabled"};
unsigned long megahertz(uint32_t mode) { return (unsigned long)(RunModes[mode].coreHz / 1000000UL); }
template <size_t N> unsigned indexOf(uint32_t value, const uint32_t (&choices)[N]) {
    for (unsigned i = 0; i < N; ++i) if (choices[i] == value) return i;
    return N;
}
void seconds(uint32_t value, const char *unit, char *output, size_t capacity) {
    if (value) std::snprintf(output, capacity, "%lu %s", (unsigned long)value, unit);
    else std::snprintf(output, capacity, "Off");
}
}
PropertyKind SettingsProperties::kind(unsigned row) const {
    const auto &entry = menu_.entry(row);
    if (entry.child != SettingsPage::Count) return PropertyKind::Page;
    if (CalibrationRows::handles(entry.action)) return calibration_.kind(entry.action);
    switch (entry.action) {
    case Action::Brightness: return PropertyKind::Number;
    case Action::ShowFps: case Action::PanDirection: case Action::HoldTime:
    case Action::BatteryDisplay: case Action::RunProfile: case Action::BufferSize:
    case Action::BacklightTimeout: case Action::BacklightIdleLevel: case Action::PowerOffTimeout:
    case Action::GeneratorStartup: case Action::UsbController: case Action::Interleave: return PropertyKind::Choice;
    default: return PropertyKind::Action;
    }
}
void SettingsProperties::name(unsigned row, char *output, size_t capacity) const {
    std::snprintf(output, capacity, "%s", menu_.entry(row).name);
}
unsigned SettingsProperties::options(unsigned row) const {
    if (CalibrationRows::handles(action(row))) return calibration_.options(action(row));
    switch (action(row)) {
    case Action::ShowFps: return 3;
    case Action::PanDirection: return 2;
    case Action::HoldTime: return 3;
    case Action::BatteryDisplay: return 3;
    case Action::RunProfile: return RunModeCount;
    case Action::BufferSize: return scope::RecordBudget::StepCount;
    case Action::BacklightTimeout: return BacklightCount;
    case Action::BacklightIdleLevel: return DimCount;
    case Action::PowerOffTimeout: return OffCount;
    case Action::GeneratorStartup: case Action::UsbController: return 2;
    case Action::Interleave: return board::Current::Features.adcInterleave ? 3 : 2;
    default: return 0;
    }
}
unsigned SettingsProperties::option(unsigned row) const {
    if (CalibrationRows::handles(action(row))) return calibration_.option(action(row));
    switch (action(row)) {
    case Action::ShowFps: return prefs_.showFps;
    case Action::PanDirection: return prefs_.panReverse;
    case Action::HoldTime: return prefs_.holdTime;
    case Action::BatteryDisplay: return prefs_.batteryDisplay;
    case Action::RunProfile: return activeRunMode_ < RunModeCount ? activeRunMode_ : prefs_.scope.runMode;
    case Action::BufferSize: return indexOf(prefs_.scope.recordPoints, scope::RecordBudget::Steps);
    case Action::BacklightTimeout: return indexOf(prefs_.idle.backlightSeconds, Backlight);
    case Action::BacklightIdleLevel: return indexOf(prefs_.idle.dimPercent, IdleSettings::DimLevels);
    case Action::PowerOffTimeout: return indexOf(prefs_.idle.powerOffMinutes, IdleSettings::OffChoices); // An old value: none.
    case Action::GeneratorStartup: return prefs_.generatorStartup;
    case Action::UsbController: return prefs_.usbController;
    case Action::Interleave: return prefs_.scope.interleave;
    default: return 0;
    }
}
void SettingsProperties::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    if (CalibrationRows::handles(action(row))) { calibration_.optionName(action(row), index, output, capacity); return; }
    switch (action(row)) {
    case Action::BatteryDisplay: std::snprintf(output, capacity, "%s", Batteries[index % 3]); break;
    case Action::ShowFps: { static constexpr const char *counters[] = {"Off", "FPS", "Sample rate"}; std::snprintf(output, capacity, "%s", counters[index % 3]); break; }
    case Action::PanDirection: std::snprintf(output, capacity, "%s", index ? "Move waveform" : "Move view"); break;
    case Action::HoldTime: { static constexpr const char *holds[] = {"1 s", "1.5 s", "2 s"}; std::snprintf(output, capacity, "%s", holds[index % 3]); break; }
    case Action::RunProfile: {
        const auto &mode = RunModes[index % RunModeCount];
        std::snprintf(output, capacity, "CPU %lu MHz  ADC %lu", (unsigned long)mode.coreHz / 1000000UL,
                      (unsigned long)mode.maximumSampleRate() / 1000000UL); // The capture maximum, MS/s.
        break;
    }
    case Action::BufferSize: {
        char points[10];
        plot::Format::points(scope::RecordBudget::Steps[index % scope::RecordBudget::StepCount], points, sizeof points);
        std::snprintf(output, capacity, "%s total points", points);
        break;
    }
    case Action::BacklightTimeout: seconds(Backlight[index % BacklightCount], "s", output, capacity); break;
    case Action::BacklightIdleLevel:
        if (IdleSettings::DimLevels[index % DimCount]) std::snprintf(output, capacity, "%lu%%", (unsigned long)IdleSettings::DimLevels[index % DimCount]);
        else std::snprintf(output, capacity, "Off");
        break;
    case Action::PowerOffTimeout: seconds(IdleSettings::OffChoices[index % OffCount], "min", output, capacity); break;
    case Action::GeneratorStartup: std::snprintf(output, capacity, "%s", index ? "Restore last" : "Off"); break;
    case Action::Interleave: // On says when it runs.
        std::snprintf(output, capacity, "%s", index == 2 ? "Auto: factory rules" : index ? "On: CH1 alone" : "Off");
        break;
    case Action::UsbController: // Enabled says where it runs.
        if (index) std::snprintf(output, capacity, "Enabled (%lu MHz only)", megahertz(usb::ClockPolicy::Mode));
        else std::snprintf(output, capacity, "Disabled");
        break;
    default: PropertyList::optionName(row, index, output, capacity); break;
    }
}
void SettingsProperties::value(unsigned row, char *output, size_t capacity) const {
    if (CalibrationRows::handles(action(row))) { calibration_.value(action(row), output, capacity); return; }
    switch (action(row)) {
    case Action::Brightness: std::snprintf(output, capacity, "%lu%%", (unsigned long)prefs_.brightness); break;
    case Action::RunProfile: { // What runs; the saved profile only where it differs (one that did not lock).
        const auto saved = (unsigned long)RunModes[prefs_.scope.runMode].coreHz / 1000000UL;
        if (activeRunMode_ >= RunModeCount) std::snprintf(output, capacity, "HSI (saved %lu)", saved);
        else if (activeRunMode_ != prefs_.scope.runMode)
            std::snprintf(output, capacity, "%lu MHz (saved %lu)", (unsigned long)RunModes[activeRunMode_].coreHz / 1000000UL, saved);
        else std::snprintf(output, capacity, "%lu MHz", (unsigned long)RunModes[activeRunMode_].coreHz / 1000000UL);
        break;
    }
    case Action::PowerOffTimeout:
        if (option(row) < OffCount) optionName(row, option(row), output, capacity);
        else std::snprintf(output, capacity, "%lu min (old)", (unsigned long)prefs_.idle.powerOffMinutes);
        break;
    case Action::GeneratorStartup: // The choice, and with Restore last what the next boot does.
        if (prefs_.generatorStartup) std::snprintf(output, capacity, "Restore last (%s)", prefs_.generatorLastOutput ? "ON" : "OFF");
        else std::snprintf(output, capacity, "Off");
        break;
    case Action::Interleave: { // Off, or On and what it does now: its rate, or the first thing it waits for.
        using C = scope::InterleaveConditions;
        const uint32_t b = interleave_.blockers;
        const char *mode = prefs_.scope.interleave == 2 ? "Auto" : "On";
        if (!prefs_.scope.interleave) std::snprintf(output, capacity, "Off");
        else if (!b)
            std::snprintf(output, capacity, "%s, active%s", mode, interleave_.calibrated ? "" : " uncal.");
        else std::snprintf(output, capacity, "%s, waits: %s", mode,
                           b & C::Channels ? "CH2 on" : b & C::Timebase ? "10/25 ns" : b & C::Rolling ? "Roll" :
                           b & C::BusLoaded ? "GEN DAC" : b & C::Internal ? "trial" : "run mode");
        break;
    }
    case Action::CalibrateInterleave: // The offset of the clock that runs, or that it has none yet.
        if (interleave_.measuring) std::snprintf(output, capacity, "Measuring...");
        else if (!interleave_.calibrated) std::snprintf(output, capacity, "Not measured");
        else {
            const int tenths = int(interleave_.offset * 10 + (interleave_.offset < 0 ? -0.5f : 0.5f));
            std::snprintf(output, capacity, "%c%d.%d codes", tenths < 0 ? '-' : '+', std::abs(tenths) / 10, std::abs(tenths) % 10);
        }
        break;
    case Action::UsbController: // The choice, and with Enabled whether the clock that runs allows it.
        if (!prefs_.usbController) std::snprintf(output, capacity, "Disabled");
        else if (usb::ClockPolicy::allows(activeRunMode_)) std::snprintf(output, capacity, "Enabled");
        else if (activeRunMode_ < RunModeCount)
            std::snprintf(output, capacity, "Enabled, off at %lu", megahertz(activeRunMode_));
        else std::snprintf(output, capacity, "Enabled, off on HSI");
        break;
    case Action::BufferSize: case Action::BacklightTimeout: case Action::BacklightIdleLevel:
        if (option(row) < options(row)) optionName(row, option(row), output, capacity);
        else if (action(row) == Action::BufferSize) {
            char points[10]; plot::Format::points(prefs_.scope.recordPoints, points, sizeof points);
            std::snprintf(output, capacity, "%s total points", points);
        } else seconds(action(row) == Action::BacklightTimeout ? prefs_.idle.backlightSeconds : prefs_.idle.dimPercent,
                       action(row) == Action::BacklightTimeout ? "s" : "%", output, capacity);
        break;
    default: PropertyList::value(row, output, capacity); break;
    }
}
bool SettingsProperties::choose(unsigned row, unsigned index) {
    if (index >= options(row)) return false;
    if (CalibrationRows::handles(action(row))) return calibration_.choose(action(row), index);
    switch (action(row)) {
    case Action::ShowFps: prefs_.showFps = index; break;
    case Action::PanDirection: prefs_.panReverse = index; break;
    case Action::HoldTime: prefs_.holdTime = index; break;
    case Action::BatteryDisplay: prefs_.batteryDisplay = index; break;
    case Action::RunProfile: prefs_.scope.runMode = index; break;
    case Action::BufferSize: prefs_.scope.recordPoints = scope::RecordBudget::Steps[index]; break;
    case Action::BacklightTimeout: prefs_.idle.backlightSeconds = Backlight[index]; break;
    case Action::BacklightIdleLevel: prefs_.idle.dimPercent = IdleSettings::DimLevels[index]; break;
    case Action::PowerOffTimeout: prefs_.idle.powerOffMinutes = IdleSettings::OffChoices[index]; break;
    case Action::GeneratorStartup: prefs_.generatorStartup = index; break;
    case Action::UsbController: prefs_.usbController = index; break;
    case Action::Interleave: prefs_.scope.interleave = index; break;
    default: return false;
    }
    return true;
}
void SettingsProperties::adjust(unsigned row, int direction) {
    if (CalibrationRows::handles(action(row))) { calibration_.adjust(action(row), direction); return; }
    if (action(row) != Action::Brightness || !direction) return;
    uint32_t value = prefs_.brightness;
    if (direction < 0 && value >= 15) value -= 5;
    if (direction > 0 && value <= 95) value += 5;
    prefs_.brightness = value;
}
int32_t SettingsProperties::number(unsigned row) const {
    if (CalibrationRows::handles(action(row))) return calibration_.number(action(row));
    return action(row) == Action::Brightness ? int32_t(prefs_.brightness) : 0;
}
void SettingsProperties::setNumber(unsigned row, int32_t value) {
    if (CalibrationRows::handles(action(row))) calibration_.setNumber(action(row), value);
    else if (action(row) == Action::Brightness) prefs_.brightness = uint32_t(value);
}
bool SettingsProperties::range(unsigned row, int32_t &minimum, int32_t &maximum) const {
    if (action(row) != Action::Brightness) return false;
    minimum = 10; maximum = 100;
    return true;
}
} // namespace openosc::ui
