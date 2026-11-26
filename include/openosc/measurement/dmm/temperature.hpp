#pragma once
#include "openosc/measurement/dmm/thermocouple.hpp"
#include <cmath>
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

class TypeK {
public:
    static constexpr double Minimum = -270, Maximum = 1372;

    // NIST Monograph 175, type K reference functions (forward EMF, mV, junction at 0 °C).
    static double emf(double celsius) {
        static constexpr double negative[] = {0, 3.94501280250e-2, 2.36223735980e-5, -3.28589067840e-7,
            -4.99048287770e-9, -6.75090591730e-11, -5.74103274280e-13, -3.10888728940e-15,
            -1.04516093650e-17, -1.98892668780e-20, -1.63226974860e-23};
        static constexpr double positive[] = {-1.76004136860e-2, 3.89212049750e-2, 1.85587700320e-5, -9.94575928740e-8,
            3.18409457190e-10, -5.60728448890e-13, 5.60750590590e-16, -3.20207200030e-19,
            9.71511471520e-23, -1.21047212750e-26};
        if (celsius == 0) return 0;
        if (celsius < 0) return polynomial(negative, celsius);
        const double t = celsius - 126.9686;
        return polynomial(positive, celsius) + 0.1185976 * std::exp(-0.0001183432 * t * t);
    }
    static bool convert(double volts, double junctionCelsius, double &celsius) {
        if (!std::isfinite(volts) || !(junctionCelsius >= Minimum && junctionCelsius <= Maximum)) return false;
        const double corrected = volts * 1000 + emf(junctionCelsius);
        double low = Minimum, high = Maximum;
        if (corrected < emf(low) || corrected > emf(high)) return false;
        // The forward curve is monotonic. Invert it to cover the full reference interval.
        for (unsigned i = 0; i < 20; ++i) {
            const double mid = (low + high) / 2;
            if (emf(mid) < corrected) low = mid;
            else high = mid;
        }
        celsius = (low + high) / 2;
        return true;
    }

private:
    template<size_t N> static double polynomial(const double (&coefficients)[N], double value) {
        double sum = coefficients[N - 1];
        for (size_t i = N - 1; i != 0;) sum = sum * value + coefficients[--i];
        return sum;
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
