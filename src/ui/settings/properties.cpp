#include "openosc/ui/settings/properties.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/platform/idle_policy.hpp"
#include "openosc/settings/run_mode.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/usb/clock_policy.hpp"
#include "openosc/settings/tooltips.hpp"
#include "openosc/acquisition/interleave.hpp"
#include <algorithm>
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
constexpr int32_t BatteryOffsetStepMv = 5;
// Preferences::usbController (the index). The data link only runs on ClockPolicy's profile.
constexpr const char *UsbControllers[] = {"Disabled", "Enabled"};
// Settings > Display > Scope shortcuts (ScopeKeys), in the order of their enums.
constexpr const char *AutoKeys[] = {"Quick: tap setup", "Classic: hold setup"};
constexpr const char *ResetHolds[] = {"Home view", "Reset zoom & pan"};
constexpr const char *Shortcuts[] = {"Cursor settings", "Measurements", "Acquisition", "Record memory", "Interleave CH1"};
static_assert(sizeof AutoKeys / sizeof *AutoKeys == unsigned(ScopeKeys::Auto::Count) &&
              sizeof ResetHolds / sizeof *ResetHolds == unsigned(ScopeKeys::Reset::Count) &&
              sizeof Shortcuts / sizeof *Shortcuts == unsigned(ScopeKeys::Shortcut::Count));
// The F4 hold's options are the destinations this board offers, in their order (Interleave last).
unsigned shortcutCount() {
    unsigned n = 0;
    for (unsigned i = 0; i < unsigned(ScopeKeys::Shortcut::Count); ++i) n += ScopeKeys::available(ScopeKeys::Shortcut(i));
    return n;
}
ScopeKeys::Shortcut shortcutAt(unsigned index) {
    for (unsigned i = 0; i < unsigned(ScopeKeys::Shortcut::Count); ++i)
        if (ScopeKeys::available(ScopeKeys::Shortcut(i)) && !index--) return ScopeKeys::Shortcut(i);
    return ScopeKeys::Shortcut::Count;
}
unsigned shortcutIndex(ScopeKeys::Shortcut shortcut) {
    unsigned n = 0;
    for (unsigned i = 0; i < unsigned(shortcut) && i < unsigned(ScopeKeys::Shortcut::Count); ++i) n += ScopeKeys::available(ScopeKeys::Shortcut(i));
    return n;
}
// The Tooltips page's rows, in Tooltip order (TooltipUsage..TooltipAdvisory).
bool tooltipRow(Action action) { return action >= Action::TooltipUsage && action <= Action::TooltipAdvisory; }
Tooltip tooltipOf(Action action) { return Tooltip(unsigned(action) - unsigned(Action::TooltipUsage)); }
static_assert(unsigned(Action::TooltipAdvisory) - unsigned(Action::TooltipUsage) + 1 == Tooltips::Optional);
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
    case Action::Brightness: case Action::BatteryGain: case Action::BatteryOffset: return PropertyKind::Number;
    case Action::SwapChannels: return PropertyKind::Toggle;
    case Action::ShowFps: case Action::ClockIndicator: case Action::PanDirection: case Action::MenuLeftKey: case Action::HoldTime:
    case Action::BatteryDisplay: case Action::RunProfile: case Action::BufferSize:
    case Action::BacklightTimeout: case Action::BacklightIdleLevel: case Action::PowerOffTimeout:
    case Action::GeneratorStartup: case Action::UsbController: case Action::Interleave:
    case Action::AutoKey: case Action::ResetHold: case Action::ShortcutHold: return PropertyKind::Choice;
    case Action::TooltipCritical: return PropertyKind::Info; // Always on: nothing to choose.
    default: return tooltipRow(entry.action) ? PropertyKind::Choice : PropertyKind::Action;
    }
}
void SettingsProperties::name(unsigned row, char *output, size_t capacity) const {
    std::snprintf(output, capacity, "%s", menu_.entry(row).name);
}
unsigned SettingsProperties::options(unsigned row) const {
    if (CalibrationRows::handles(action(row))) return calibration_.options(action(row));
    switch (action(row)) {
    case Action::ShowFps: return 3;
    case Action::ClockIndicator: case Action::SwapChannels: return 2;
    case Action::PanDirection: case Action::MenuLeftKey: return 2;
    case Action::HoldTime: return 3;
    case Action::BatteryDisplay: return 3;
    case Action::RunProfile: return RunModeCount;
    case Action::BufferSize: return scope::RecordBudget::StepCount;
    case Action::BacklightTimeout: return BacklightCount;
    case Action::BacklightIdleLevel: return DimCount;
    case Action::PowerOffTimeout: return OffCount;
    case Action::GeneratorStartup: case Action::UsbController: return 2;
    case Action::Interleave: return board::Current::Features.interleaveControl() ? 3 : 2; // Off, On, Auto.
    case Action::AutoKey: return unsigned(ScopeKeys::Auto::Count);
    case Action::ResetHold: return unsigned(ScopeKeys::Reset::Count);
    case Action::ShortcutHold: return shortcutCount();
    default: return tooltipRow(action(row)) ? 2 : 0; // Off, On.
    }
}
unsigned SettingsProperties::option(unsigned row) const {
    if (CalibrationRows::handles(action(row))) return calibration_.option(action(row));
    switch (action(row)) {
    case Action::ShowFps: return prefs_.showFps;
    case Action::ClockIndicator: return prefs_.clockIndicator;
    case Action::SwapChannels: return prefs_.swapChannels;
    case Action::PanDirection: return prefs_.panReverse;
    case Action::MenuLeftKey: return prefs_.menuLeftStep;
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
    case Action::AutoKey: return unsigned(prefs_.scope.keys.automatic);
    case Action::ResetHold: return unsigned(prefs_.scope.keys.reset);
    case Action::ShortcutHold: return shortcutIndex(prefs_.scope.keys.destination());
    default: return tooltipRow(action(row)) && Tooltips::shown(prefs_.tooltipsHidden, tooltipOf(action(row)));
    }
}
void SettingsProperties::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    if (CalibrationRows::handles(action(row))) { calibration_.optionName(action(row), index, output, capacity); return; }
    switch (action(row)) {
    case Action::BatteryDisplay: std::snprintf(output, capacity, "%s", Batteries[index % 3]); break;
    case Action::ShowFps: { static constexpr const char *counters[] = {"Off", "FPS", "Sample rate"}; std::snprintf(output, capacity, "%s", counters[index % 3]); break; }
    case Action::PanDirection: std::snprintf(output, capacity, "%s", index ? "Move waveform" : "Move view"); break;
    case Action::MenuLeftKey: std::snprintf(output, capacity, "%s", index ? "Step" : "Back"); break;
    case Action::ClockIndicator: case Action::SwapChannels: case Action::TooltipUsage: case Action::TooltipUnavailable: case Action::TooltipProgress:
    case Action::TooltipDone: case Action::TooltipAdvisory: std::snprintf(output, capacity, "%s", index ? "On" : "Off"); break;
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
        // Auto: the ZT706S factory's rule, the ZT703S's as MKR firmware (CH1 alone at 10/25 ns, 200/280 MHz).
        std::snprintf(output, capacity, "%s", index == 2 ? (board::Current::Features.adcInterleave ? "Auto: factory rules" : "Auto: CH1, 10/25 ns")
                                              : index ? "On: CH1 alone" : "Off");
        break;
    case Action::AutoKey: std::snprintf(output, capacity, "%s", AutoKeys[index % unsigned(ScopeKeys::Auto::Count)]); break;
    case Action::ResetHold: std::snprintf(output, capacity, "%s", ResetHolds[index % unsigned(ScopeKeys::Reset::Count)]); break;
    case Action::ShortcutHold: {
        const auto shortcut = shortcutAt(index);
        std::snprintf(output, capacity, "%s", shortcut < ScopeKeys::Shortcut::Count ? Shortcuts[unsigned(shortcut)] : "");
        break;
    }
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
    case Action::TooltipCritical: std::snprintf(output, capacity, "Always on"); break;
    case Action::BatteryGain: { // +1.5 %: 0.1 % steps.
        const int gain = prefs_.batteryGainPermille;
        std::snprintf(output, capacity, "%s%d.%d %%", gain < 0 ? "-" : gain ? "+" : "", std::abs(gain) / 10, std::abs(gain) % 10);
        break;
    }
    case Action::BatteryOffset: std::snprintf(output, capacity, prefs_.batteryOffsetMv ? "%+d mV" : "%d mV", int(prefs_.batteryOffsetMv)); break;
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
    case Action::ClockIndicator: prefs_.clockIndicator = index; break;
    case Action::SwapChannels: prefs_.swapChannels = index; break;
    case Action::PanDirection: prefs_.panReverse = index; break;
    case Action::MenuLeftKey: prefs_.menuLeftStep = index != 0; break;
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
    case Action::AutoKey: prefs_.scope.keys.automatic = ScopeKeys::Auto(index); break;
    case Action::ResetHold: prefs_.scope.keys.reset = ScopeKeys::Reset(index); break;
    case Action::ShortcutHold: prefs_.scope.keys.shortcut = shortcutAt(index); break;
    default:
        if (!tooltipRow(action(row))) return false;
        prefs_.tooltipsHidden = Tooltips::set(prefs_.tooltipsHidden, tooltipOf(action(row)), index != 0);
        break;
    }
    return true;
}
void SettingsProperties::adjust(unsigned row, int direction) {
    if (CalibrationRows::handles(action(row))) { calibration_.adjust(action(row), direction); return; }
    const auto field = action(row);
    if (field == Action::BatteryGain || field == Action::BatteryOffset) {
        using Gauge = power::BatteryGauge;
        const bool offset = field == Action::BatteryOffset;
        const int32_t limit = offset ? Gauge::TrimOffsetMv : Gauge::TrimGainPermille;
        const int32_t step = offset ? BatteryOffsetStepMv : 1;
        setNumber(row, std::clamp(number(row) + direction * step, -limit, limit));
        return;
    }
    if (action(row) != Action::Brightness || !direction) return;
    uint32_t value = prefs_.brightness;
    if (direction < 0 && value >= 15) value -= 5;
    if (direction > 0 && value <= 95) value += 5;
    prefs_.brightness = value;
}
// Battery gain: 0.1 % a step, 1 % on Up/Down; offset: 5 mV a step, 50 mV on Up/Down.
int SettingsProperties::coarseStep(unsigned row) const {
    return action(row) == Action::BatteryGain || action(row) == Action::BatteryOffset ? 10 : 1;
}
int32_t SettingsProperties::number(unsigned row) const {
    if (CalibrationRows::handles(action(row))) return calibration_.number(action(row));
    switch (action(row)) {
    case Action::Brightness: return int32_t(prefs_.brightness);
    case Action::BatteryGain: return prefs_.batteryGainPermille;
    case Action::BatteryOffset: return prefs_.batteryOffsetMv;
    default: return 0;
    }
}
void SettingsProperties::setNumber(unsigned row, int32_t value) {
    if (CalibrationRows::handles(action(row))) { calibration_.setNumber(action(row), value); return; }
    using Gauge = power::BatteryGauge;
    switch (action(row)) {
    case Action::Brightness:
        prefs_.brightness = uint32_t(value);
        break;
    case Action::BatteryGain:
        if (Gauge::trimValid(value, 0)) prefs_.batteryGainPermille = int8_t(value);
        break;
    case Action::BatteryOffset:
        if (Gauge::trimValid(0, value)) prefs_.batteryOffsetMv = int16_t(value);
        break;
    default: break;
    }
}
bool SettingsProperties::range(unsigned row, int32_t &minimum, int32_t &maximum) const {
    if (action(row) != Action::Brightness) return false; // Calibration values need their signed text.
    minimum = 10; maximum = 100;
    return true;
}
} // namespace openosc::ui
