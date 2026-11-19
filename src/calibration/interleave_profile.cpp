#include "openosc/calibration/interleave_profile.hpp"
#include <cmath>

namespace openosc::calibration {
bool InterleaveProfile::valid(const CalibrationData &factory) const {
    if (!factory.valid() || factoryCrc != factory.crc() || !std::isfinite(offset) || !allowed(offset) || clockedCount > Clocks)
        return false;
    for (unsigned i = 0; i < clockedCount; ++i) {
        if (!timing(uint32_t(clocked[i].coreMhz) * 1000000U, clocked[i].adcDivider) || !std::isfinite(clocked[i].offset) ||
            !allowed(clocked[i].offset)) return false;
        for (unsigned j = 0; j < i; ++j)
            if (clocked[j].coreMhz == clocked[i].coreMhz && clocked[j].adcDivider == clocked[i].adcDivider) return false;
    }
    return true;
}
} // namespace openosc::calibration
