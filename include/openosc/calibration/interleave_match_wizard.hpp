#pragma once
#include "openosc/acquisition/frame.hpp"

namespace openosc::calibration {
class InterleaveMatchWizard {
public:
    enum class Step : uint8_t { Reference, Measuring, Review, Failed };
    enum class Issue : uint8_t { None, NoFrame, WrongFrequency, Level, Impure, Fit, Validation };
    static constexpr unsigned Training = 6, Verification = 5, Count = Training + Verification;
    static constexpr uint32_t Points = 4096, SettleMs = 100, TimeoutMs = 3000;

    void begin(uint32_t clockHz, const scope::FrameMetadata &metadata);
    bool sameSetup(uint32_t clockHz, const scope::FrameMetadata &metadata) const;
    void startTone(uint32_t now, uint64_t instrumentTimeUs);
    void noFrame();
    void take(const scope::FrameView &frame);
    bool settled(uint32_t now) const { return uint32_t(now - at_) >= SettleMs; }
    bool expired(uint32_t now) const { return uint32_t(now - at_) >= TimeoutMs; }
    bool fresh(const scope::FrameMetadata &metadata) const { return metadata.timestampUs >= afterUs_; }
    Step step() const { return step_; }
    Issue issue() const { return issue_; }
    unsigned index() const { return index_; }
    unsigned frequencyMHz() const;
    float improvementDb() const { return improvement_[Verification / 2]; }
    uint32_t clockHz() const { return clockHz_; }
    const Rational &rate() const { return rate_; }
    const scope::ChannelMetadata &channel() const { return channel_; }
    const scope::InterleaveMatch &filter() const { return filter_; }

private:
    bool fit();
    void acceptVerification(double before, double after);
    struct Ratio { float omega, real, imag; };
    Ratio training_[Training]{};
    float improvement_[Verification]{};
    scope::InterleaveMatch filter_;
    scope::ChannelMetadata channel_;
    Rational rate_;
    uint64_t afterUs_ = 0;
    uint32_t clockHz_ = 0, at_ = 0;
    unsigned index_ = 0;
    Step step_ = Step::Reference;
    Issue issue_ = Issue::None;
    void retry(Issue issue) { step_ = Step::Reference; issue_ = issue; }
    void fail(Issue issue) { step_ = Step::Failed; issue_ = issue; }
};
} // namespace openosc::calibration
