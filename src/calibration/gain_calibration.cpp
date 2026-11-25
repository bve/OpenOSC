#include "openosc/calibration/gain_calibration.hpp"
#include "openosc/calibration/scope_calibration.hpp"
#include "openosc/calibration/pwm_profile.hpp"
#include "openosc/acquisition/ranges.hpp"
#include <cmath>
#include <cstdio>

namespace openosc::calibration {
bool GainCalibration::expectedSpan(const CalibrationData &factory, unsigned channel, unsigned range, double tipVolts,
                                   uint32_t numerator, uint32_t denominator, double &codes) {
    double voltsPerCode = 0, zero = 0;
    if (!numerator || !denominator || !std::isfinite(tipVolts) ||
        !factory.scopeScale(channel, scope::VoltageRanges::millivolts(range), ZeroProfile::Positions[Point], voltsPerCode, zero) ||
        !(voltsPerCode > 0)) return false;
    codes = tipVolts * denominator / numerator / voltsPerCode; // At the input: the probe ratio, once.
    return true;
}
void GainCalibration::configure(unsigned channel, unsigned range, double referenceVolts) {
    channel_ = channel < GainProfile::Channels ? channel : 0;
    range_ = range < GainProfile::Ranges ? range : 0;
    reference_ = referenceVolts;
}
const char *GainCalibration::requirement() const {
    switch (step_) {
    case Step::Ground: return "Keep the probe tip on GND";
    case Step::RetryGround: return "Disconnect signal and touch tip to GND";
    case Step::AwaitReference: return "Apply the reference to the tip";
    case Step::Reference: return "Keep the reference on the tip";
    case Step::Review: return "Save or cancel the result";
    }
    return "";
}
bool GainCalibration::available() const { return file_.available() && file_.loaded() && calibration_.valid(); }
bool GainCalibration::fail(Failure failure) {
    failure_ = failure; state_ = State::InvalidInput;
    return false;
}
bool GainCalibration::rejectReading(Failure failure, uint32_t now) {
    if (step_ != Step::Ground) return fail(failure);
    failure_ = failure;
    step_ = Step::RetryGround;
    deadline_ = now + UserMs;
    return false;
}
void GainCalibration::measure(Step step, uint32_t now) {
    step_ = step; state_ = State::Settling;
    sampler_.reset(range_, Samples, now);
    deadline_ = now + MeasureMs;
}
void GainCalibration::begin(const ScopeRequest &original, uint16_t factoryCrc, uint32_t now) {
    original_ = original; point_ = Point; failure_ = Failure::None;
    ground_ = measured_ = 0; proposed_ = 1; inForce_ = calibration_.gain().factor(channel_, range_);
    measure(Step::Ground, now);
    double zero = 0, span = 0;
    if (factoryCrc != calibration_.factory().crc() ||
        !calibration_.factory().scopeScale(channel_, scope::VoltageRanges::millivolts(range_), ZeroProfile::Positions[Point], factoryVoltsPerCode_, zero) ||
        !expectedSpan(calibration_.factory(), channel_, range_, reference_, original.probeNumerator[channel_], original.probeDenominator[channel_], span)) {
        state_ = State::Uncalibrated; return;
    }
    if (!spanAllowed(span)) fail(Failure::Reference);
}
void GainCalibration::applied(uint32_t at) {
    if (active() && (step_ == Step::Ground || step_ == Step::Reference)) measure(step_, at);
}
void GainCalibration::tick(uint32_t now) {
    if (!active()) return;
    if (int32_t(now - deadline_) >= 0) { state_ = State::Timeout; return; }
    if (state_ == State::Settling && sampler_.settled(now)) state_ = State::Sampling;
}
bool GainCalibration::observe(const scope::FrameView &frame, uint32_t now) {
    tick(now);
    if (state_ != State::Sampling || waiting()) return false;
    const auto taken = sampler_.take(frame, calibration_.factory().crc());
    if (taken == ZeroSampler::Take::Uncalibrated) { state_ = State::Uncalibrated; return false; }
    if (taken == ZeroSampler::Take::None || !sampler_.complete()) return false;
    const auto s = sampler_.statistics(channel_);
    if (s.minimum <= -125 || s.maximum >= 125) return rejectReading(Failure::Saturated, now);
    if (s.variance > 9 || s.outliers > sampler_.samples() / ZeroSampler::OutlierDivisor || sampler_.drift(channel_) > DriftCodes)
        return rejectReading(Failure::Unstable, now);
    if (step_ == Step::Ground) {
        // The deviation includes the input voltage and any error in the offset calibration.
        if (std::abs(s.mean - sampler_.expected(channel_)) > PwmProfile::MaximumShiftCodes)
            return rejectReading(Failure::Ground, now);
        ground_ = s.mean; step_ = Step::AwaitReference; deadline_ = now + UserMs;
        return false;
    }
    measured_ = s.mean;
    const double codes = span(), input = reference_ * original_.probeDenominator[channel_] / original_.probeNumerator[channel_];
    if (std::abs(codes) < MinimumSpanCodes) return fail(Failure::Span);
    if ((codes > 0) != (input > 0)) return fail(Failure::WrongSign);
    proposed_ = input / (codes * factoryVoltsPerCode_);
    if (!GainProfile::allowed(proposed_)) return fail(Failure::Implausible);
    step_ = Step::Review; deadline_ = now + UserMs;
    return false;
}
bool GainCalibration::proceed(uint32_t now) {
    if (!active()) return false;
    switch (step_) {
    case Step::RetryGround:
        failure_ = Failure::None;
        measure(Step::Ground, now);
        return true;
    case Step::AwaitReference:
        measure(Step::Reference, now);
        return true;
    default:
        return false;
    }
}
bool GainCalibration::accept() {
    if (!active() || step_ != Step::Review) return false;
    state_ = State::Complete;
    return true;
}
bool GainCalibration::commit() {
    if (state_ != State::Complete || !file_.loaded()) return false;
    // Every other cell as saved (unmeasured ones 1).
    const auto values = file_.values().withCell(calibration_.factory().crc(), channel_, range_, proposed_);
    if (!file_.save(values)) return false;
    return calibration_.setGain(values);
}
unsigned GainCalibration::percent() const {
    const unsigned measured = unsigned(uint64_t(sampler_.samples()) * 50 / Samples);
    switch (step_) {
    case Step::RetryGround: return 0;
    case Step::Ground: return measured;
    case Step::AwaitReference: return 50;
    case Step::Reference: return 50 + measured;
    case Step::Review: return 100;
    }
    return 0;
}
void GainCalibration::detail(char *output, size_t capacity) const {
    static constexpr const char *Steps[] = {"CH%u ground", "CH%u wait reference", "CH%u reference", "CH%u review", "CH%u retry ground"};
    std::snprintf(output, capacity, Steps[unsigned(step_)], channel_ + 1);
}
const char *GainCalibration::reason() const {
    switch (failure_) {
    case Failure::Reference: return "failed: reference span not 40..110 codes";
    case Failure::Ground: return "failed: tip level outside zero range";
    case Failure::Span: return "failed: span under 40 codes";
    case Failure::Saturated: return "failed: input at the ADC limit";
    case Failure::Unstable: return "failed: input not stable";
    case Failure::WrongSign: return "failed: reference of the wrong sign";
    case Failure::Implausible: return "failed: correction outside 0.8..1.25";
    case Failure::None: break;
    }
    return nullptr;
}
} // namespace openosc::calibration
