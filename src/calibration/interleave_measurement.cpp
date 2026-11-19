#include "openosc/calibration/interleave_measurement.hpp"
#include "openosc/calibration/interleave_profile.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::calibration {
void InterleaveOffsetMeasurement::begin(uint32_t now, uint32_t coreHz, uint32_t first) {
    *this = InterleaveOffsetMeasurement{};
    began_ = now; coreHz_ = coreHz; first_ = first;
    result_ = Result::Running;
}
bool InterleaveOffsetMeasurement::record(const scope::FrameView &frame) {
    if (!active() || !frame.valid()) return false;
    const auto &m = frame.metadata();
    // Only a record of this measurement: interleaved, not Roll, uncorrected, new.
    if (m.layout != scope::Layout::InterleavedCh1 || m.rolling || m.interleaveOffset != 0 ||
        int32_t(m.sequence - first_) < 0 || (seen_ && m.sequence == last_))
        return false;
    seen_ = true; last_ = m.sequence;
    const uint32_t n = std::min(m.samples, MaximumPoints);
    auto cursor = frame.cursor(scope::Channel::Ch1);
    // A point's ADC is its ring position's parity (even: HIGH, odd: LOW); the ring is even, so the
    // parity alternates through the wrap too.
    unsigned adc = (m.origin & 1U) ? 1U : 0U;
    int16_t code = 0;
    for (uint32_t i = 0; i < n && cursor.next(code); ++i, adc ^= 1U) {
        sum_[adc] += code; squares_[adc] += double(code) * code; ++count_[adc];
        rails_[adc] += code >= 127 || code <= -128;
    }
    if (++records_ >= Records) finish();
    return true;
}
void InterleaveOffsetMeasurement::finish() {
    if (!count_[0] || !count_[1]) { result_ = Result::NotQuiet; return; }
    double mean[2], rms[2];
    for (unsigned a = 0; a < 2; ++a) {
        mean[a] = sum_[a] / double(count_[a]);
        rms[a] = std::sqrt(std::max(0.0, squares_[a] / double(count_[a]) - mean[a] * mean[a]));
    }
    spread_ = std::max(rms[0], rms[1]);
    offset_ = float(mean[1] - mean[0]);
    const bool clipped = double(rails_[0]) > ClippedShare * double(count_[0]) || double(rails_[1]) > ClippedShare * double(count_[1]);
    result_ = clipped ? Result::Clipped : spread_ > SpreadLimit ? Result::NotQuiet
            : !InterleaveProfile::allowed(offset_) ? Result::OutOfRange : Result::Measured;
}
} // namespace openosc::calibration
