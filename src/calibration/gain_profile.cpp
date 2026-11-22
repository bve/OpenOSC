#include "openosc/calibration/gain_profile.hpp"
#include <cmath>

namespace openosc::calibration {
bool GainProfile::valid(const CalibrationData &factory) const {
    if (!factory.valid() || factoryCrc != factory.crc()) return false;
    for (const auto &channel : gain) for (float factor : channel) if (!std::isfinite(factor) || !allowed(factor)) return false;
    return true;
}
double GainProfile::factor(unsigned channel, unsigned range) const {
    return enabled && channel < Channels && range < Ranges ? gain[channel][range] : 1.0;
}
bool GainProfile::cellSavable(unsigned channel, unsigned range) const {
    if (enabled) return true;
    for (unsigned c = 0; c < Channels; ++c)
        for (unsigned r = 0; r < Ranges; ++r) if ((c != channel || r != range) && gain[c][r] != 1.0f) return false;
    return true;
}
GainProfile GainProfile::withCell(uint16_t crc, unsigned channel, unsigned range, double value) const {
    auto out = *this;
    out.factoryCrc = crc; out.enabled = true;
    if (channel < Channels && range < Ranges) out.gain[channel][range] = float(value);
    return out;
}
} // namespace openosc::calibration
