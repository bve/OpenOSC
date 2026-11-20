#include "openosc/data/units.hpp"
#include <cmath>
#include <cstring>

namespace openosc {
bool UnitParser::parse(const char *text, const char *unit, NumericValue &output) {
    if (!text || !unit || !*text)
        return false;
    bool negative = *text == '-';
    if (*text == '-' || *text == '+')
        ++text;
    double value = 0, divisor = 1;
    bool point = false, digit = false;
    unsigned length = 0;
    for (; *text && length < 16; ++text, ++length) {
        if (*text == '.' && !point) {
            point = true;
            continue;
        }
        if (*text < '0' || *text > '9')
            return false;
        digit = true;
        value = value * 10 + (*text - '0');
        if (point)
            divisor *= 10;
    }
    if (*text || !digit)
        return false;
    struct Entry {
        const char *unit;
        Quantity quantity;
        double scale;
    };
    static constexpr Entry units[] = {
        {"V", Quantity::Voltage, 1},         {"mV", Quantity::Voltage, 1e-3},
        {"uV", Quantity::Voltage, 1e-6},     {"A", Quantity::Current, 1},
        {"mA", Quantity::Current, 1e-3},     {"uA", Quantity::Current, 1e-6},
        {"&", Quantity::Resistance, 1},      {"K&", Quantity::Resistance, 1e3},
        {"M&", Quantity::Resistance, 1e6},   {"Ohm", Quantity::Resistance, 1},
        {"kOhm", Quantity::Resistance, 1e3}, {"MOhm", Quantity::Resistance, 1e6},
        {"F", Quantity::Capacitance, 1},     {"mF", Quantity::Capacitance, 1e-3},
        {"uF", Quantity::Capacitance, 1e-6}, {"nF", Quantity::Capacitance, 1e-9},
        {"Hz", Quantity::Frequency, 1},      {"KHz", Quantity::Frequency, 1e3},
        {"kHz", Quantity::Frequency, 1e3},  {"x", Quantity::Temperature, 1},
        {"degC", Quantity::Temperature, 1}, {"degF", Quantity::Temperature, 5. / 9}};
    for (auto entry : units)
        if (!std::strcmp(unit, entry.unit)) {
            value = value / divisor * entry.scale * (negative ? -1 : 1);
            if (!std::strcmp(unit, "degF")) value -= 32 * entry.scale;
            if (!std::isfinite(value))
                return false;
            output = {value, entry.quantity, DataStatus::Ok};
            return true;
        }
    return false;
}
} // namespace openosc
