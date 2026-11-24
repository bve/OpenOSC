#include "openosc/acquisition/rolling.hpp"

namespace openosc::scope {
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
    const uint64_t ticks = uint64_t(metadata.samples) * metadata.samplesPerSecond.denominator;
    if (ticks > UINT64_MAX / 1000000)
        return false;
    const uint64_t duration = ticks * 1000000 / metadata.samplesPerSecond.numerator;
    if (metadata.timestampUs > UINT64_MAX - duration)
        return false;
    for (size_t i = 0; i < frame.metadata().samples; ++i) {
        uint16_t word = 0;
        for (unsigned c = 0; c < 2; ++c) {
            uint8_t raw = 0;
            if (frame.metadata().channels[c].enabled && !frame.code(static_cast<Channel>(c), i, raw))
                return false;
            word |= uint16_t(raw) << (c * 8);
        }
        storage_[cursor_] = word;
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
    const uint64_t ticks = uint64_t(size_) * metadata.samplesPerSecond.denominator;
    if (ticks > UINT64_MAX / 1000000)
        return {};
    const uint64_t duration = ticks * 1000000 / metadata.samplesPerSecond.numerator;
    if (duration > nextTimestampUs_)
        return {};
    metadata.timestampUs = nextTimestampUs_ - duration;
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
