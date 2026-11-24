#include "openosc/ui/editors/meter_properties.hpp"
#include "openosc/measurement/dmm/ranges.hpp"
#include <cstdio>
#include "openosc/ui/format/meter.hpp"
#include "openosc/ui/components/meter_style.hpp"
#include "openosc/settings/instrument_preferences.hpp"
#include "openosc/data/decimal.hpp"
#include <algorithm>

namespace openosc::ui {
PropertyKind MeterProperties::kind(unsigned row) const {
    if (row == Range && !ranged()) return PropertyKind::Info; // Diode, continuity: nothing to choose.
    switch (row) {
    case Range: case Display: case Colors: case Digits: case JunctionSource: case TemperatureUnit: return PropertyKind::Choice;
    case ColdJunction: return PropertyKind::Number;
    case ResetStatistics: return PropertyKind::Action;
    case Appearance: case Layout: case Hold: return PropertyKind::Page; // Own lists (MeterAppearance/MeterHold/ArrangementProperties).
    default: return PropertyKind::Toggle;
    }
}
void MeterProperties::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"Range", "Display", "Base colors", "Base digits", "Appearance", "Layout", "Statistics", "Bar graph",
                                            "Bar scale unit", "Relative",
                                            "Hold", "Reset statistics", "CSV logging", "Junction source", "Manual junction", "Temp units", "Serial 115200"};
    std::snprintf(output, capacity, "%s", row < Count ? names[row] : "");
}
unsigned MeterProperties::options(unsigned row) const {
    switch (row) {
    case Range: case TemperatureUnit: return 2;
    case JunctionSource: return unsigned(dmm::JunctionSource::Count);
    case Display: return MeterPreferences::Displays;
    case Colors: return MeterPreferences::Palettes;
    case Digits: return MeterPreferences::Faces;
    default: return kind(row) == PropertyKind::Toggle ? 2 : 0;
    }
}
unsigned MeterProperties::option(unsigned row) const {
    switch (row) {
    case Range: return automatic() ? 0 : 1;
    case Display: return display_;
    case Colors: return palette_;
    case Digits: return face_;
    case Statistics: return statistics_;
    case BarGraph: return bar_;
    case BarUnit: return barUnit_;
    case Relative: return relative_;
    case Logging: return logging_;
    case SerialOutput: return serial_;
    case JunctionSource: return unsigned(temperature_.source);
    case TemperatureUnit: return temperature_.fahrenheit;
    default: return 0;
    }
}
void MeterProperties::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    static constexpr const char *displays[] = {"Primary", "Statistics", "Trend", "Analog"};
    switch (row) {
    case Range: std::snprintf(output, capacity, "%s", index ? "Manual" : "Auto"); break;
    case Display: std::snprintf(output, capacity, "%s", displays[index < MeterPreferences::Displays ? index : 0]); break;
    case Colors: std::snprintf(output, capacity, "%s", MeterStyles::paletteName(index)); break;
    case Digits: std::snprintf(output, capacity, "%s", MeterStyles::faceName(index)); break;
    case JunctionSource: {
        static constexpr const char *sources[] = {"Manual", "MCU at startup", "MCU live"};
        std::snprintf(output, capacity, "%s", index < options(row) ? sources[index] : "");
        break;
    }
    case TemperatureUnit: std::snprintf(output, capacity, "%s", index ? "°F" : "°C"); break;
    default: PropertyList::optionName(row, index, output, capacity); break;
    }
}
void MeterProperties::value(unsigned row, char *output, size_t capacity) const {
    if (row == Range && !ranged()) std::snprintf(output, capacity, "None");
    else if (row == Range && result_.range == dmm::RangeMode::Unknown) std::snprintf(output, capacity, "Unknown");
    else if (row == SerialOutput && jackBusy_) std::snprintf(output, capacity, "GEN on");
    else if (row == Hold) std::snprintf(output, capacity, "%s", dmm::HoldSettings::name(hold_));
    else if (row == JunctionSource && temperature_.source != dmm::JunctionSource::Manual) {
        const char *source = temperature_.source == dmm::JunctionSource::McuStartup ? "Startup" : "Live";
        char value[16];
        if (mcuJunction_ && Decimal::fixed(*mcuJunction_ / 10.0, 1, value, sizeof value))
            std::snprintf(output, capacity, "%s %s °C", source, value);
        else std::snprintf(output, capacity, "%s --", source);
    }
    else if (row == ColdJunction) {
        char value[16]{};
        if (temperature_.manualConfigured() && Decimal::fixed(temperature_.junctionCelsius(), 1, value, sizeof value))
            std::snprintf(output, capacity, "%s °C", value);
        else std::snprintf(output, capacity, "Set °C");
    }
    else PropertyList::value(row, output, capacity);
}
// The choices are taken at once by the controller (meterChanged), one changed row at a time; it
// reports what it could not apply.
bool MeterProperties::choose(unsigned row, unsigned index) {
    if (index >= options(row)) return false;
    if (row == Range && !ranged()) return false;
    switch (row) {
    case Range: result_.range = index ? dmm::RangeMode::Manual : dmm::RangeMode::Auto; break;
    case Display: display_ = index; break;
    case Colors: palette_ = index; break;
    case Digits: face_ = index; break;
    case Statistics: statistics_ = index; break;
    case BarGraph: bar_ = index; break;
    case BarUnit: barUnit_ = index; break;
    case Relative: relative_ = index; break;
    case Logging: logging_ = index; break;
    case SerialOutput: serial_ = index; break;
    case JunctionSource: temperature_.source = dmm::JunctionSource(index); break;
    case TemperatureUnit: temperature_.fahrenheit = index != 0; break;
    default: return false;
    }
    return true;
}
int32_t MeterProperties::number(unsigned row) const {
    return row == ColdJunction ? temperature_.junctionDeciC : 0;
}
void MeterProperties::setNumber(unsigned row, int32_t value) {
    if (row == ColdJunction) temperature_.junctionDeciC = value;
}
void MeterProperties::beginEdit(unsigned row) {
    if (row == ColdJunction && !temperature_.manualConfigured()) setNumber(row, dmm::TemperatureSettings::DefaultJunction);
}
void MeterProperties::adjust(unsigned row, int step) {
    if (row == ColdJunction)
        setNumber(row, std::clamp(number(row) + step, dmm::TemperatureSettings::Minimum, dmm::TemperatureSettings::Maximum));
}
bool MeterProperties::ranged() const { return dmm::Ranges::selectable(dmm::Functions::identify(result_)); }
bool MeterProperties::optionSwatch(unsigned row, unsigned index, uint32_t &rgb) const {
    if (row != Colors || !theme_ || index >= MeterStyles::Schemes) return false;
    const auto palette = MeterStyles::palette(index, *theme_);
    rgb = index == MeterStyles::Amber ? palette.ink : palette.background; // Dark and Amber backgrounds are both near black.
    return true;
}
} // namespace openosc::ui
