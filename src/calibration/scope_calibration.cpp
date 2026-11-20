#include "openosc/calibration/scope_calibration.hpp"
#include "openosc/acquisition/ranges.hpp"
#include <cmath>

namespace openosc::calibration {
bool ScopeCalibration::node(unsigned channel, unsigned range, unsigned point, uint16_t &compare) const {
    uint16_t row[PwmProfile::Points];
    if (point >= PwmProfile::Points || !factory_.offsetRow(channel, range, row)) return false;
    compare = pwm_.enabled ? pwm_.compare[channel][range][point] : row[point];
    return true;
}
bool ScopeCalibration::offsetCompare(unsigned channel, unsigned range, int position, uint16_t &compare) const {
    if (channel >= PwmProfile::Channels || range >= PwmProfile::Ranges) return false;
    if (nominal()) return interpolateCompare(NominalRow, position, compare); // No trial or profile without a record.
    const auto &trial = trial_[channel];
    if (trial.active && trial.range == range && position == ZeroProfile::Positions[trial.point]) {
        compare = trial.compare;
        return valid();
    }
    if (!pwm_.enabled) return factory_.offsetCompare(channel, range, position, compare);
    return valid() && interpolateCompare(pwm_.compare[channel][range], position, compare);
}
bool ScopeCalibration::setPwm(const PwmProfile &profile) {
    if (profile.enabled && !profile.valid(factory_)) return false;
    pwm_ = profile;
    ++generation_;
    return true;
}
bool ScopeCalibration::scale(unsigned channel, unsigned range, int position, double &voltsPerCode, double &zeroCode,
                             double displayGain) const {
    if (channel >= PwmProfile::Channels || range >= GainProfile::Ranges) return false;
    const uint32_t millivolts = scope::VoltageRanges::millivolts(range);
    if (nominal() ? !CalibrationData::transfer(NominalGain, millivolts, position, displayGain, voltsPerCode, zeroCode)
                  : !factory_.scopeScale(channel, millivolts, position, voltsPerCode, zeroCode, displayGain)) return false;
    voltsPerCode *= gain_.factor(channel, range);
    return true;
}
bool ScopeCalibration::setGain(const GainProfile &profile) {
    if (profile.enabled && !profile.valid(factory_)) return false;
    gain_ = profile;
    return true;
}
float ScopeCalibration::interleaveOffset() const {
    if (interleave_.enabled) return interleave_.offset;
    if (nominal()) return NominalInterleave;
    const float factory = factory_.interleaveCorrection();
    return std::isfinite(factory) && InterleaveProfile::allowed(factory) ? factory : 0;
}
bool ScopeCalibration::setInterleave(const InterleaveProfile &profile) {
    if (profile.enabled && !profile.valid(factory_)) return false;
    interleave_ = profile;
    return true;
}
void ScopeCalibration::setTrial(unsigned channel, unsigned range, unsigned point, uint16_t compare) {
    if (channel >= PwmProfile::Channels || range >= PwmProfile::Ranges || point >= PwmProfile::Points || compare > 9999) return;
    trial_[channel] = {true, range, point, compare};
    ++generation_;
}
void ScopeCalibration::clearTrial() {
    if (!trial_[0].active && !trial_[1].active) return;
    trial_[0] = trial_[1] = {};
    ++generation_;
}
} // namespace openosc::calibration
