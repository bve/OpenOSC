#include "openosc/acquisition/rolling.hpp"

namespace openosc::scope {
namespace {
// The time `points` take at `rate`, in microseconds, without the 64-bit product of ticks and 10^6.
bool micros(const Rational &rate, uint64_t points, uint64_t &output) {
    const uint64_t ticks = points * rate.denominator, seconds = ticks / rate.numerator;
    if (points > UINT64_MAX / rate.denominator || seconds > UINT64_MAX / 1000000 - 1) return false;
    output = seconds * 1000000 + ticks % rate.numerator * 1000000 / rate.numerator;
    return true;
}
} // namespace
bool RollingBuffer::append(const FrameView &frame) {
    if (!storage_ || !capacity_ || capacity_ > 65536 || !frame.valid())
        return false;
    const auto &metadata = frame.metadata();
    if (continuous_) {
        const uint64_t delta = metadata.timestampUs > nextTimestampUs_
                                   ? metadata.timestampUs - nextTimestampUs_
                                   : nextTimestampUs_ - metadata.timestampUs;
        if (delta > 1 || metadata.samplesPerSecond.numerator != metadata_.samplesPerSecond.numerator ||
            metadata.samplesPerSecond.denominator != metadata_.samplesPerSecond.denominator ||
            metadata.timingVerified != metadata_.timingVerified)
            return false;
        for (unsigned c = 0; c < 2; ++c) {
            const auto &a = metadata.channels[c];
            const auto &b = metadata_.channels[c];
            if (a.enabled != b.enabled || a.coupling != b.coupling ||
                a.millivoltsPerDivision != b.millivoltsPerDivision || a.probeNumerator != b.probeNumerator ||
                a.probeDenominator != b.probeDenominator || a.calibrated != b.calibrated ||
                a.voltsPerCode != b.voltsPerCode || a.zeroCode != b.zeroCode ||
                a.calibrationCrc != b.calibrationCrc)
                return false;
        }
    }
    Rational stored;
    if (!metadata.samplesPerSecond.scaled(1, averaging_, stored)) return false; // A snapshot could not say its rate.
    uint64_t duration = 0;
    if (!micros(metadata.samplesPerSecond, metadata.samples, duration) || metadata.timestampUs > UINT64_MAX - duration)
        return false;
    for (size_t i = 0; i < frame.metadata().samples; ++i) {
        uint8_t raw[2] = {};
        for (unsigned c = 0; c < 2; ++c)
            if (frame.metadata().channels[c].enabled && !frame.code(static_cast<Channel>(c), i, raw[c]))
                return false;
        if (averaging_ > 1) {
            for (unsigned c = 0; c < 2; ++c) sums_[c] += int8_t(raw[c]);
            if (++pending_ < averaging_) continue;
            const int32_t n = int32_t(averaging_);
            for (unsigned c = 0; c < 2; ++c) {
                raw[c] = uint8_t(int8_t((sums_[c] < 0 ? sums_[c] - n / 2 : sums_[c] + n / 2) / n));
                sums_[c] = 0;
            }
            pending_ = 0;
        }
        storage_[cursor_] = uint16_t(raw[0] | raw[1] << 8);
        cursor_ = (cursor_ + 1) % capacity_;
        if (size_ < capacity_)
            ++size_;
        else
            ++dropped_;
    }
    metadata_ = metadata;
    nextTimestampUs_ = metadata.timestampUs + duration;
    continuous_ = true;
    return true;
}
FrameView RollingBuffer::snapshot() const {
    if (!continuous_ || !size_)
        return {};
    auto metadata = metadata_;
    metadata.layout = Layout::Linear;
    metadata.interleaveOffset = 0; // The history already holds the corrected codes.
    metadata.samples = size_;
    metadata.ring = capacity_;
    metadata.origin = (cursor_ + capacity_ - size_) % capacity_;
    metadata.rollPhase = dropped_; // The oldest retained point's index in the history.
    metadata.clearTrigger();
    // The stored points end where the points still summed begin; each lies at the middle of its own.
    const Rational input = metadata_.samplesPerSecond;
    uint64_t pending = 0, duration = 0, middle = 0;
    if (!input.scaled(1, averaging_, metadata.samplesPerSecond) || !micros(input, pending_, pending) ||
        !micros(input, uint64_t(size_) * averaging_, duration) || !micros(input, averaging_ - 1, middle) ||
        pending + duration > nextTimestampUs_)
        return {};
    metadata.timebaseNs *= averaging_;
    metadata.timestampUs = nextTimestampUs_ - pending - duration + middle / 2;
    const uint16_t *banks[] = {storage_, nullptr, nullptr, nullptr};
    return {metadata, banks, capacity_};
}
bool RollingBuffer::code(Channel channel, size_t index, uint8_t &output) const {
    if (!storage_ || !capacity_ || index >= size_ || static_cast<unsigned>(channel) > 1)
        return false;
    const size_t first = (cursor_ + capacity_ - size_) % capacity_;
    output = uint8_t(storage_[(first + index) % capacity_] >> (static_cast<unsigned>(channel) * 8));
    return true;
}
} // namespace openosc::scope
