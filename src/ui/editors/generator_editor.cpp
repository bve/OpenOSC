#include "openosc/ui/editors/generator_editor.hpp"
#include "openosc/generator/frequency.hpp"
#include "openosc/ui/scope/format.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::ui {
namespace {
using Caps = generator::Capabilities;
using Row = GeneratorEditor::Row;
constexpr Row DacWaveRows[] = {Row::Shape, Row::Duty, Row::Level, Row::Frequency, Row::File};
constexpr Row ModuleWaveRows[] = {Row::Shape, Row::Duty, Row::Amplitude, Row::Frequency};
constexpr const Row *WaveRows = Caps::Arbitrary ? DacWaveRows : ModuleWaveRows;
constexpr unsigned WaveCount = Caps::Arbitrary ? sizeof DacWaveRows / sizeof *DacWaveRows : sizeof ModuleWaveRows / sizeof *ModuleWaveRows;
// Layout last: the screen's block order (GENERATOR LAYOUT), not a setting of the output.
constexpr Row DacParameters[] = {Row::Duty, Row::Level, Row::Drive, Row::MinimumCode, Row::MaximumCode, Row::Preset, Row::Frequency, Row::Layout};
constexpr Row ModuleParameters[] = {Row::Duty, Row::Amplitude, Row::Preset, Row::Frequency, Row::Layout};
constexpr auto Presets = GeneratorEditor::Presets;
constexpr unsigned PresetCount = GeneratorEditor::PresetCount;
// The reference has a place only where main OUTPUT had one: the ZT703S DAC/PWM jack.
constexpr unsigned PresetOptions = PresetCount + (Caps::Levels ? 1 : 0);
// Only a Dedicated square has a drive to choose; elsewhere the row tells the path in use.
bool drives(const generator::Settings &s) { return s.shape == generator::Shape::Square && s.level == generator::Level::Dedicated; }
}
unsigned GeneratorEditor::count() const {
    if (wave_) return WaveCount;
    return Caps::Levels ? sizeof DacParameters / sizeof *DacParameters : sizeof ModuleParameters / sizeof *ModuleParameters;
}
GeneratorEditor::Row GeneratorEditor::row(unsigned index) const {
    const Row *rows = wave_ ? WaveRows : Caps::Levels ? DacParameters : ModuleParameters;
    return index < count() ? rows[index] : Row::Frequency;
}
PropertyKind GeneratorEditor::kind(unsigned index) const {
    switch (row(index)) {
    case Row::Level: return draft_.settings.arbitrary() && wave_ ? PropertyKind::Info : PropertyKind::Choice; // AWG: the waveform's codes.
    case Row::Shape: case Row::Preset: return PropertyKind::Choice;
    case Row::File: return PropertyKind::Action; // MENU opens the AWG file list.
    case Row::Drive: return drives(draft_.settings) ? PropertyKind::Choice : PropertyKind::Info;
    case Row::Duty: return Caps::duty(draft_.settings.shape) ? PropertyKind::Number : PropertyKind::Info;
    case Row::Amplitude: case Row::MinimumCode: case Row::MaximumCode: return PropertyKind::Number;
    case Row::Frequency: return PropertyKind::Info; // Set in the frequency editor.
    case Row::Layout: return PropertyKind::Action;  // MENU opens GENERATOR LAYOUT.
    }
    return PropertyKind::Info;
}
void GeneratorEditor::name(unsigned index, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"Wave", "Duty", "Level", "Amplitude", "DAC min", "DAC max", "Preset", "Frequency", "Square out", "Layout", "AWG file"};
    const Row r = row(index);
    std::snprintf(output, capacity, "%s", r == Row::Frequency && wave_ && draft_.settings.arbitrary() ? "Sample rate" : names[unsigned(r)]);
}
unsigned GeneratorEditor::options(unsigned index) const {
    switch (row(index)) {
    case Row::Shape: return generator::OfferedShapes;
    case Row::Level: return 4;
    case Row::Preset: return PresetOptions;
    case Row::Drive: return drives(draft_.settings) ? 2 : 0;
    default: return 0;
    }
}
unsigned GeneratorEditor::option(unsigned index) const {
    switch (row(index)) {
    case Row::Shape: return unsigned(draft_.settings.shape);
    case Row::Level: return unsigned(draft_.settings.level);
    case Row::Drive: return unsigned(draft_.settings.squareDrive);
    case Row::Preset: // A frequency off the presets is none of them until one is chosen.
        for (unsigned i = 0; i < PresetCount; ++i) if (draft_.settings.frequencyHz.value() == Presets[i]) return i;
        return PresetOptions;
    default: return 0;
    }
}
void GeneratorEditor::optionName(unsigned index, unsigned option, char *output, size_t capacity) const {
    switch (row(index)) {
    case Row::Shape: std::snprintf(output, capacity, "%s", generator::ShapeNames[option % generator::ShapeCount]); break;
    case Row::Level: std::snprintf(output, capacity, "%s", generator::Levels::Names[option % 4]); break;
    case Row::Drive: std::snprintf(output, capacity, "%s", option ? "PWM always" : "Factory: DAC <1kHz"); break;
    case Row::Preset:
        if (option == Reference) std::snprintf(output, capacity, "Reference 1kHz");
        else plot::Format::quantity(Presets[option % PresetCount], "Hz", output, capacity);
        break;
    default: if (capacity) output[0] = 0; break;
    }
}
void GeneratorEditor::limit() {
    if (draft_.settings.arbitrary()) return; // Its own rate: the other shapes' frequency is kept as it is.
    const uint64_t maximum = Caps::maximumMillihertz(draft_.settings.shape, draft_.settings.level, timerHz_);
    if (generator::Frequency::millihertz(draft_.settings.frequencyHz) > maximum) {
        draft_.settings.frequencyHz = generator::Frequency::rational(maximum); draft_.clamped = true;
    }
    if (Caps::Amplitude) {
        const uint16_t top = Caps::amplitudeLimitMillivolts(draft_.settings.shape, generator::Frequency::millihertz(draft_.settings.frequencyHz));
        if (draft_.settings.amplitudeMillivolts > top) { draft_.settings.amplitudeMillivolts = top; draft_.amplitudeClamped = true; }
    }
}
bool GeneratorEditor::choose(unsigned index, unsigned option) {
    if (option >= options(index)) return false;
    switch (row(index)) {
    case Row::Shape: draft_.settings.shape = generator::Shape(option); break;
    case Row::Level: draft_.settings.level = generator::Level(option); break;
    case Row::Drive: draft_.settings.squareDrive = generator::SquareDrive(option); break;
    case Row::Preset:
        if (option == Reference) { // main OUTPUT selector 0 (generator::referenceSettings): its shape, level, duty, 1 kHz.
            const auto reference = generator::referenceSettings();
            draft_.settings.shape = reference.shape; draft_.settings.level = reference.level;
            draft_.settings.dutyPermille = reference.dutyPermille;
            draft_.settings.frequencyHz = reference.frequencyHz;
        } else draft_.settings.frequencyHz = {Presets[option], 1};
        break;
    default: return false;
    }
    limit();
    return true;
}
int32_t GeneratorEditor::number(unsigned index) const {
    switch (row(index)) {
    case Row::Duty: return draft_.settings.dutyPermille;
    case Row::Amplitude: return draft_.settings.amplitudeMillivolts;
    case Row::MinimumCode: case Row::MaximumCode: {
        const auto codes = generator::Levels::codes(draft_.settings);
        return row(index) == Row::MinimumCode ? codes.minimum : codes.maximum;
    }
    default: return 0;
    }
}
void GeneratorEditor::adjust(unsigned index, int direction) {
    if (!direction) return;
    const int step = direction > 0 ? 1 : -1;
    switch (row(index)) {
    case Row::Duty:
        if (Caps::duty(draft_.settings.shape))
            draft_.settings.dutyPermille = uint16_t(std::clamp<int>(int(draft_.settings.dutyPermille) + step * Caps::DutyStepPermille,
                                                              Caps::MinimumDutyPermille, Caps::MaximumDutyPermille));
        break;
    case Row::Amplitude:
        if (Caps::Amplitude)
            draft_.settings.amplitudeMillivolts = uint16_t(std::clamp<int>(int(draft_.settings.amplitudeMillivolts) + step * Caps::AmplitudeStepMillivolts, 0,
                Caps::amplitudeLimitMillivolts(draft_.settings.shape, generator::Frequency::millihertz(draft_.settings.frequencyHz))));
        break;
    case Row::MinimumCode: case Row::MaximumCode: {
        const auto codes = generator::Levels::codes(draft_.settings);
        draft_.settings.minimumCode = codes.minimum; draft_.settings.maximumCode = codes.maximum;
        draft_.settings.level = generator::Level::Custom;
        if (row(index) == Row::MinimumCode) draft_.settings.minimumCode = uint16_t(std::clamp<int>(int(draft_.settings.minimumCode) + step * 25, 0, draft_.settings.maximumCode));
        else draft_.settings.maximumCode = uint16_t(std::clamp<int>(int(draft_.settings.maximumCode) + step * 25, draft_.settings.minimumCode, 4095));
        break;
    }
    default: break;
    }
    limit();
}
void GeneratorEditor::value(unsigned index, char *output, size_t capacity) const {
    char requested[24], maximum[24];
    switch (row(index)) {
    case Row::Level:
        if (wave_ && draft_.settings.arbitrary()) { std::snprintf(output, capacity, "DAC codes 0-4095"); return; }
        PropertyList::value(index, output, capacity);
        return;
    case Row::Duty:
        if (Caps::duty(draft_.settings.shape)) std::snprintf(output, capacity, "%u.%u%%", draft_.settings.dutyPermille / 10, draft_.settings.dutyPermille % 10);
        else std::snprintf(output, capacity, "Not available");
        return;
    case Row::Amplitude: { // Nominal: the module's output voltage is not calibrated.
        const unsigned top = Caps::amplitudeLimitMillivolts(draft_.settings.shape, generator::Frequency::millihertz(draft_.settings.frequencyHz));
        std::snprintf(output, capacity, "%u.%u V (max %u.%u) nom.", draft_.settings.amplitudeMillivolts / 1000,
                      draft_.settings.amplitudeMillivolts % 1000 / 100, top / 1000, top % 1000 / 100);
        return;
    }
    case Row::MinimumCode: case Row::MaximumCode:
        std::snprintf(output, capacity, "%ld codes", long(number(index)));
        return;
    case Row::Preset:
        plot::Format::quantity(draft_.settings.frequencyHz.value(), "Hz", output, capacity);
        return;
    case Row::Layout: std::snprintf(output, capacity, ">"); return;
    case Row::File: // The loaded waveform and its file, or what MENU does.
        if (!arbitrary_.points) std::snprintf(output, capacity, "None: MENU to load");
        else if (*arbitrary_.file) std::snprintf(output, capacity, "%s, %u pts", arbitrary_.file, arbitrary_.points);
        else std::snprintf(output, capacity, "USB data, %u pts", arbitrary_.points);
        return;
    case Row::Drive: // The choice, then the path it gives at this frequency (or the path of any other output).
        if (drives(draft_.settings))
            std::snprintf(output, capacity, "%s, now %s", generator::Levels::DriveNames[unsigned(draft_.settings.squareDrive) & 1],
                          generator::Levels::pwm(draft_.settings) ? "PWM" : "DAC");
        else std::snprintf(output, capacity, "%s", generator::Levels::pwm(draft_.settings) ? "PWM" : "DAC");
        return;
    case Row::Frequency:
        if (wave_ && draft_.settings.arbitrary()) { // The rate, and the frequency it repeats the loaded points at.
            plot::Format::quantity(draft_.settings.arbitrarySamplesPerSecond, "S/s", requested, sizeof requested);
            if (arbitrary_.points) {
                plot::Format::quantity(double(draft_.settings.arbitrarySamplesPerSecond) / arbitrary_.points, "Hz", maximum, sizeof maximum);
                std::snprintf(output, capacity, "%s = %s", requested, maximum);
            } else std::snprintf(output, capacity, "%s", requested);
            return;
        }
        plot::Format::quantity(draft_.settings.frequencyHz.value(), "Hz", requested, sizeof requested);
        plot::Format::quantity(Caps::maximumMillihertz(draft_.settings.shape, draft_.settings.level, timerHz_) / 1000.0, "Hz", maximum, sizeof maximum);
        std::snprintf(output, capacity, "%s (max %s)", requested, maximum);
        return;
    default: PropertyList::value(index, output, capacity); return;
    }
}
} // namespace openosc::ui
