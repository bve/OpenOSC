#include "openosc/ui/scope/source.hpp"
#include "openosc/acquisition/adc_bus_glitch.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
Source::Source(const scope::FrameView &frame, const Options &options, Statistics &statistics)
    : frame_(frame), options_(options), statistics_(statistics) {
    valid_ = frame.valid();
    const auto &metadata = frame.metadata();
    count_ = metadata.samples;
    origin_ = metadata.origin;
    ring_ = metadata.ring;
    mask_ = ring_ - 1;
    layout_ = metadata.layout;
    powerOfTwo_ = ring_ && (ring_ & (ring_ - 1)) == 0;
    for (unsigned i = 0; i < 4; ++i)
        banks_[i] = frame.bank(i);
    split_ = frame.split();
    const auto *c = frame.metadata().channels;
    for (unsigned i = 0; i < 2; ++i)
        enabled_[i] = read_[i] = c[i].enabled && options.drawn(i);
    bool compatible = c[0].enabled && c[1].enabled && c[0].calibrated == c[1].calibrated;
    if (compatible && c[0].calibrated) {
        const double ratio = c[1].voltsPerCode * c[1].probeNumerator * c[0].probeDenominator /
                             (c[0].voltsPerCode * c[0].probeNumerator * c[1].probeDenominator);
        compatible = std::isfinite(ratio) && ratio >= 1.0 / 16384 && ratio <= 16384 &&
                     std::abs(c[1].zeroCode) <= 32768;
        if (compatible) {
            ratioQ24_ = int64_t(std::llround(ratio * 16777216));
            zero2Q8_ = int32_t(std::lround(c[1].zeroCode * 256));
        }
    } else if (compatible) {
        compatible = c[0].millivoltsPerDivision == c[1].millivoltsPerDivision &&
                     uint64_t(c[0].probeNumerator) * c[1].probeDenominator ==
                         uint64_t(c[1].probeNumerator) * c[0].probeDenominator;
    }
    enabled_[2] = options.math != Math::Off && compatible;
    if (enabled_[2])
        read_[0] = read_[1] = true;
}
uint8_t Source::byteAt(uint32_t index, unsigned channel) const {
    uint32_t position = uint32_t(origin_ + index);
    position = powerOfTwo_ ? position & mask_ : position % ring_;
    if (layout_ == scope::Layout::FourByteBanks)
        return reinterpret_cast<const uint8_t *>(banks_[position & 3])[position >> 2];
    const uint16_t word =
        layout_ == scope::Layout::Linear ? banks_[0][position] : this->word(position);
    return uint8_t(word >> (channel * 8));
}
__attribute__((noinline, cold)) uint8_t Source::resolved(uint8_t byte, uint32_t index, unsigned channel) const {
    if (count_ < 2) return byte;
    return scope::AdcBusGlitch::resolve(byte, byteAt(index ? index - 1 : index + 1, channel),
                                        byteAt(index + 1 < count_ ? index + 1 : index - 1, channel));
}
bool Source::sample(uint32_t index, int32_t (&v)[3]) {
    if (!valid_ || index >= count_)
        return false;
    v[0] = v[1] = v[2] = 0;
    if (!read_[0] && !read_[1])
        return true;
    if (layout_ == scope::Layout::InterleavedCh1) { // CH1 alone: the frame's own pairs, correction and bus rule.
        if (!frame_.interleavedQ8(index, v[0])) return false;
        ++statistics_.sourceReads;
        return true;
    }
    uint32_t position = uint32_t(origin_ + index);
    position = powerOfTwo_ ? position & mask_ : position % ring_;
    uint16_t word;
    if (layout_ == scope::Layout::FourByteBanks) {
        // The one enabled channel's byte, placed in that channel's half of the word.
        const uint8_t byte = reinterpret_cast<const uint8_t *>(banks_[position & 3])[position >> 2];
        word = read_[1] ? uint16_t(byte << 8) : byte;
    } else {
        word = layout_ == scope::Layout::Linear ? banks_[0][position] : this->word(position);
    }
    for (unsigned c = 0; c < 2; ++c)
        if (read_[c]) {
            unsigned byte = (word >> (c * 8)) & 255;
            if (__builtin_expect(scope::AdcBusGlitch::suspect(uint8_t(byte)), 0))
                byte = resolved(uint8_t(byte), index, c);
            const int raw = byte < 128 ? int(byte) : int(byte) - 256;
            v[c] = int32_t(raw) * 256;
            ++statistics_.sourceReads;
        }
    if (enabled_[2]) {
        const int64_t second = (int64_t(v[1]) - zero2Q8_) * ratioQ24_ / 16777216;
        v[2] = int32_t(std::clamp<int64_t>(int64_t(v[0]) + (options_.math == Math::Add ? second : -second),
                                           -16777216, 16777216));
    }
    return true;
}
} // namespace openosc::plot
