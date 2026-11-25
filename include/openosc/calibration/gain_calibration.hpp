#pragma once
#include "openosc/calibration/zero_procedure.hpp"
#include "openosc/calibration/zero_sampler.hpp"
#include "openosc/calibration/gain_profile.hpp"

namespace openosc::calibration {
class ScopeCalibration;
// The gain of one channel and range against a known DC reference, offset-independent: the mean
// code with the probe tip grounded, then (once the user says the reference is applied) with the
// reference on it, both at node Y = 80 under the offsets in force. The span between them against
// the reference (at the tip, through the channel's probe ratio, once) gives the factor on the
// factory's volts per code (absolute: a repeated run measures the same, nothing compounds).
// Nothing is saved until the user takes the result (Save): accept() completes, and ZeroOperation
// commits it (scope_gain.json, then in force) and restores the original capture.
class GainCalibration final : public ZeroProcedure {
public:
    enum class Step : uint8_t { Ground, AwaitReference, Reference, Review, RetryGround };
    enum class Failure : uint8_t { None, Reference, Ground, Span, Saturated, Unstable, WrongSign, Implausible };
    static constexpr unsigned Point = 3; // Y = 80: the most room on either side.
    static constexpr uint32_t Samples = 65536, MeasureMs = 10000, UserMs = 300000;
    // The reference must move the input this many codes (the factory's scale): less resolves the
    // gain too coarsely, more leaves no room for noise before the ADC's ends.
    static constexpr double MinimumSpanCodes = 40, MaximumSpanCodes = 110, DriftCodes = 1;
    GainCalibration(ScopeCalibration &calibration, GainStore &file) : calibration_(calibration), file_(file) {}
    // The cell and the reference (volts at the probe tip, either sign); before begin.
    void configure(unsigned channel, unsigned range, double referenceVolts);
    // The span a reference should give at the factory scale through a probe ratio; false: no scale.
    static bool expectedSpan(const CalibrationData &factory, unsigned channel, unsigned range, double tipVolts,
                             uint32_t probeNumerator, uint32_t probeDenominator, double &codes);
    static bool spanAllowed(double codes) { return codes == codes && (codes >= MinimumSpanCodes || codes <= -MinimumSpanCodes) &&
                                                   codes <= MaximumSpanCodes && codes >= -MaximumSpanCodes; }
    const char *title() const override { return "GAIN CALIBRATION"; }
    const char *name() const override { return "Gain calibration"; }
    const char *requirement() const override;
    bool available() const override; // The disk is there and scope_gain.json was read.
    uint32_t durationMs() const override { return 2 * MeasureMs + 2 * UserMs; }
    void begin(const ScopeRequest &original, uint16_t factoryCrc, uint32_t now) override;
    ScopeRequest trial() override { return ZeroSampler::trial(original_, range_, Point); }
    void applied(uint32_t at) override;
    bool observe(const scope::FrameView &frame, uint32_t now) override;
    void tick(uint32_t now) override;
    bool commit() override;
    unsigned percent() const override;
    void detail(char *output, size_t capacity) const override;
    uint32_t samples() const override { return sampler_.samples(); }
    bool waiting() const override { return active() && (step_ == Step::RetryGround || step_ == Step::AwaitReference || step_ == Step::Review); }
    const char *reason() const override;
    // The user has moved the tip to GND or applied the reference.
    bool proceed(uint32_t now);
    bool accept();
    Step step() const { return step_; }
    Failure failure() const { return failure_; }
    unsigned channel() const { return channel_; }
    double referenceVolts() const { return reference_; }
    double groundCode() const { return ground_; }
    double referenceCode() const { return measured_; }
    double span() const { return measured_ - ground_; }
    double proposed() const { return proposed_; }
    double inForce() const { return inForce_; } // The cell's factor when the measurement began.
private:
    void measure(Step step, uint32_t now);
    bool fail(Failure failure);
    bool rejectReading(Failure failure, uint32_t now);
    ScopeCalibration &calibration_;
    GainStore &file_;
    ZeroSampler sampler_;
    Step step_ = Step::Ground;
    Failure failure_ = Failure::None;
    unsigned channel_ = 0;
    double reference_ = 0, ground_ = 0, measured_ = 0, proposed_ = 1, factoryVoltsPerCode_ = 0, inForce_ = 1;
    uint32_t deadline_ = 0;
};
} // namespace openosc::calibration
