#pragma once
#include "openosc/data/status.hpp"
#include <cmath>

namespace openosc {
enum class Quantity : uint8_t { Unknown, Voltage, Current, Resistance, Capacitance, Frequency, Temperature };
struct NumericValue {
    double si = 0;
    Quantity quantity = Quantity::Unknown;
    DataStatus status = DataStatus::NoData;
    bool valid() const {
        return status == DataStatus::Ok && quantity > Quantity::Unknown && quantity <= Quantity::Temperature &&
               std::isfinite(si);
    }
};
class UnitParser {
public:
    static bool parse(const char *number, const char *unit, NumericValue &output);
};
} // namespace openosc
