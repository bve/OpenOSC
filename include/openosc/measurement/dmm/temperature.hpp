#pragma once
#include "openosc/measurement/dmm/thermocouple.hpp"
#include <cstdint>
#include <cstring>
#include <optional>

namespace openosc::dmm {
enum class JunctionSource : uint8_t { Manual, McuStartup, McuLive, Count };

struct TemperatureSettings {
    static constexpr int32_t Minimum = -500, Maximum = 1000, Unset = Minimum - 1;
    static constexpr int32_t DefaultJunction = 250;
    int32_t junctionDeciC = DefaultJunction;
    bool fahrenheit = false;
    JunctionSource source = JunctionSource::Manual;
    ThermocoupleType type = ThermocoupleType::K;
    constexpr bool manualConfigured() const { return junctionDeciC >= Minimum && junctionDeciC <= Maximum; }
    constexpr bool configured() const { return source != JunctionSource::Manual || manualConfigured(); }
    constexpr bool valid() const {
        return source < JunctionSource::Count && type < ThermocoupleType::Count && (manualConfigured() || junctionDeciC == Unset);
    }
    constexpr bool operator==(const TemperatureSettings &other) const {
        return junctionDeciC == other.junctionDeciC && fahrenheit == other.fahrenheit && source == other.source &&
               type == other.type;
    }
    constexpr double junctionCelsius() const { return junctionDeciC / 10.0; }
    // The cold junction in effect (degC): the manual value, or the MCU reading; none while it is not known.
    std::optional<double> junctionInEffect(std::optional<int16_t> mcuDeciC) const {
        if (source == JunctionSource::Manual) return manualConfigured() ? std::optional<double>(junctionCelsius()) : std::nullopt;
        return mcuDeciC ? std::optional<double>(*mcuDeciC / 10.0) : std::nullopt;
    }
    // The junction in effect is known and outside the window of the type.
    bool junctionOutsideWindow(std::optional<int16_t> mcuDeciC) const {
        const auto junction = junctionInEffect(mcuDeciC);
        return junction && !Thermocouple::junctionInRange(type, *junction);
    }
};

class TemperatureUnits {
public:
    static const char *unit(bool fahrenheit) { return fahrenheit ? "degF" : "degC"; }
    static bool fahrenheit(const char *unit) { return std::strcmp(unit, "degF") == 0; }
    static double display(double celsius, const char *unit, bool relative = false) {
        return fahrenheit(unit) ? celsius * 1.8 + (relative ? 0 : 32) : celsius;
    }
};
} // namespace openosc::dmm
