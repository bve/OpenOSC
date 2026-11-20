#pragma once

namespace openosc {
struct MeterFunctionInfo { const char* key; const char* name; const char* unit; };
inline constexpr MeterFunctionInfo MeterFunctions[] = {
    {"voltage_dc", "DC voltage", "V"}, {"voltage_ac", "AC voltage", "V"},
    {"resistance", "Resistance", "Ohm"}, {"continuity", "Continuity", "Ohm"},
    {"diode", "Diode", "V"}, {"capacitance", "Capacitance", "F"},
    {"current_dc", "DC current", "A"}, {"current_ac", "AC current", "A"},
    {"millivolts_dc", "DC millivolts", "mV"}, {"millivolts_ac", "AC millivolts", "mV"},
    {"milliamps_dc", "DC milliamps", "mA"}, {"milliamps_ac", "AC milliamps", "mA"},
    {"temperature_k", "Temperature (Type K)", "degC"}
};
inline constexpr unsigned MeterFunctionCount = sizeof MeterFunctions / sizeof *MeterFunctions;
}
