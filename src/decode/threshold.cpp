#include "openosc/decode/threshold.hpp"
#include "openosc/decode/acceleration/plateau.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace openosc::decoding {
namespace {
// The band's codes before any range check: a rise at `high` or above, a fall at `low` or below.
// False without a scale.
bool band(const scope::ChannelMetadata &c, int32_t thresholdMv, uint32_t hysteresisMv, double &high, double &low) {
    if (!c.calibrated || !(c.voltsPerCode > 0) || !c.probeNumerator || !c.probeDenominator) return false;
    const double codesPerVolt = double(c.probeDenominator) / (c.voltsPerCode * c.probeNumerator);
    const auto at = [&](double millivolts) { return c.zeroCode + millivolts / 1000.0 * codesPerVolt; };
    const double half = hysteresisMv / 2.0;
    high = std::ceil(at(thresholdMv + half)); low = std::floor(at(thresholdMv - half));
    return true;
}
} // namespace
ThresholdCodes ThresholdCodes::of(const scope::ChannelMetadata &c, int32_t thresholdMv, uint32_t hysteresisMv) {
    ThresholdCodes codes;
    double high, low;
    if (!band(c, thresholdMv, hysteresisMv, high, low)) return codes;
    if (!std::isfinite(high) || !std::isfinite(low) || high > 127 || low < -128) { codes.status = Status::OutOfRange; return codes; }
    codes.low = int16_t(low);
    codes.high = int16_t(high > low ? high : low + 1);
    if (codes.high > 127) { codes.status = Status::OutOfRange; return codes; }
    codes.status = Status::Ok;
    return codes;
}
bool DifferenceScale::of(const scope::FrameMetadata &m, DifferenceScale &out) {
    double perCode[2], widest = 0;
    for (unsigned c = 0; c < 2; ++c) {
        const auto &ch = m.channels[c];
        if (!ch.enabled || !ch.calibrated || !(ch.voltsPerCode > 0) || !ch.probeNumerator || !ch.probeDenominator) return false;
        perCode[c] = ch.voltsPerCode * ch.probeNumerator / ch.probeDenominator * 1e6;
        for (int code : {-128, 127}) widest = std::max(widest, std::fabs((code - ch.zeroCode) * perCode[c]));
    }
    constexpr double Limit = (1 << 30) - 1; // Every entry within it: a difference stays within int32.
    out.shift = 0;
    while (out.shift < 40 && widest / std::ldexp(1.0, out.shift) + 0.5 > Limit) ++out.shift;
    if (!std::isfinite(widest) || out.shift >= 40) return false;
    const double unit = std::ldexp(1.0, out.shift);
    for (unsigned c = 0; c < 2; ++c)
        for (int code = -128; code < 128; ++code)
            out.units[c][uint8_t(code)] = int32_t(std::lround((code - m.channels[c].zeroCode) * perCode[c] / unit));
    return true;
}
void ThresholdStream::reset(uint32_t minPulsePoints, uint8_t channel) {
    channel_ = channel; minPulse_ = minPulsePoints;
    level_ = initial_ = -1; pending_ = false; pendingAt_ = initialAt_ = glitches_ = skipped_ = clipped_ = 0;
}
void ThresholdStream::begin(const ThresholdCodes &codes, uint32_t minPulsePoints, scope::Channel channel) {
    reset(minPulsePoints, uint8_t(channel));
    high_ = codes.high; low_ = codes.low; difference_ = nullptr;
}
namespace {
// A non-negative value rounded to nearest into uint32 (saturated): no long (32 bits on the device) involved.
uint32_t roundedMv(long double mv) {
    if (!(mv > 0)) return 0;
    return mv >= 4294967294.5L ? UINT32_MAX : uint32_t(uint64_t(mv + 0.5L));
}
} // namespace
uint32_t ThresholdCodes::autoHysteresisMv(const scope::ChannelMetadata &c, uint32_t swing, unsigned percent) {
    if (!c.calibrated || !(c.voltsPerCode > 0) || !c.probeNumerator || !c.probeDenominator) return 0;
    return roundedMv((long double)swing * c.voltsPerCode * c.probeNumerator / c.probeDenominator * 1000.0L * percent / 100.0L);
}
ThresholdCodes ThresholdCodes::automatic(const scope::ChannelMetadata &c, int32_t thresholdMv, int32_t lowest, int32_t highest,
                                         unsigned percent, uint32_t floorMv, uint32_t &hysteresisMv) {
    const uint32_t swing = highest >= lowest ? uint32_t(int64_t(highest) - lowest) : 0;
    const uint32_t share = autoHysteresisMv(c, swing, percent);
    hysteresisMv = share > floorMv ? share : floorMv;
    ThresholdCodes codes = of(c, thresholdMv, hysteresisMv);
    if (codes.status != Status::OutOfRange || share >= floorMv || of(c, thresholdMv, share).status != Status::Ok) return codes;
    // Only the floor left the codes: each level beyond them is the rail (reached) or no code at all.
    double high, low;
    if (!band(c, thresholdMv, hysteresisMv, high, low) || !std::isfinite(high) || !std::isfinite(low)) return codes;
    if (high > 127) high = highest >= 127 ? 127 : 128;
    if (low < -128) low = lowest <= -128 ? -128 : -129;
    codes.low = int16_t(low);
    codes.high = int16_t(high > low ? high : low + 1);
    codes.status = Status::Ok;
    return codes;
}
uint32_t DifferenceScale::autoHysteresisMv(uint64_t swing, unsigned percent) const {
    // swing * 2^shift uV * percent / 100, in mV (swing < 2^31, shift < 32, percent <= 100).
    return roundedMv((long double)swing * (long double)(uint64_t(1) << shift) * percent / 100000.0L);
}
void LevelRange::scan(const scope::FrameView &frame, uint32_t first, uint32_t n) {
    if (!n) return;
    int32_t lowest = lowest_, highest = highest_;
    if (difference_) {
        auto cursor = frame.pairCursor(first);
        const auto &u = difference_->units;
        int16_t a, b;
        for (uint32_t i = 0; i < n && cursor.next(a, b); ++i) {
            const int32_t v = u[0][uint8_t(a)] - u[1][uint8_t(b)]; // Exact (DifferenceScale).
            lowest = v < lowest ? v : lowest; highest = v > highest ? v : highest;
        }
    } else {
        auto cursor = frame.cursor(channel_, first);
        cursor.run(n, [&](int16_t code) __attribute__((always_inline)) {
            lowest = code < lowest ? code : lowest; highest = code > highest ? code : highest;
        });
    }
    lowest_ = lowest; highest_ = highest;
}
void ThresholdStream::beginDifference(const DifferenceScale &scale, int32_t thresholdMv, uint32_t hysteresisMv,
                                      uint32_t minPulsePoints, uint8_t channel) {
    reset(minPulsePoints, channel);
    // In the table's unit: high rounded up, low down (a level needs at least as much as in microvolts).
    const int64_t center = int64_t(thresholdMv) * 1000, half = int64_t(hysteresisMv) * 500;
    const int64_t unit = int64_t(1) << scale.shift, high = center + half, low = center - half;
    const auto floorDiv = [](int64_t a, int64_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); };
    high_ = int32_t(-floorDiv(-high, unit)); low_ = int32_t(floorDiv(low, unit));
    if (high_ <= low_) high_ = low_ + 1;
    difference_ = &scale;
}
inline void ThresholdStream::feed(int32_t code, uint32_t index, Transition *out, unsigned &count) {
    if (level_ < 0) {
        if (code >= high_) { level_ = initial_ = 1; initialAt_ = index; }
        else if (code <= low_) { level_ = initial_ = 0; initialAt_ = index; }
        return;
    }
    if (pending_) { // The other level, held until it lasts minPulse_ points.
        const bool reverted = level_ ? code >= high_ : code <= low_;
        if (reverted) { pending_ = false; ++glitches_; return; }
        if (index - pendingAt_ + 1 < minPulse_) return;
        level_ ^= 1; pending_ = false;
        out[count++] = {pendingAt_, uint8_t(level_), channel_};
        return;
    }
    const bool change = level_ ? code <= low_ : code >= high_;
    if (!change) return;
    if (minPulse_ > 1) { pending_ = true; pendingAt_ = index; return; }
    level_ ^= 1;
    out[count++] = {index, uint8_t(level_), channel_};
}
unsigned ThresholdStream::scanDifference(const scope::FrameView &frame, uint32_t first, uint32_t n, Transition *out) {
    unsigned count = 0;
    auto cursor = frame.pairCursor(first);
    const auto &u = difference_->units;
    int16_t a, b;
    for (uint32_t index = first, end = first + n; index < end && cursor.next(a, b); ++index) {
        clipped_ += a == -128 || a == 127 || b == -128 || b == 127;
        feed(u[0][uint8_t(a)] - u[1][uint8_t(b)], index, out, count); // |each| < 2^30: exact (DifferenceScale).
    }
    return count;
}
unsigned ThresholdStream::scan(const scope::FrameView &frame, uint32_t first, uint32_t n, Transition *out, bool scalarOnly) {
    unsigned count = 0;
    if (!n) return 0;
    if (difference_) return scanDifference(frame, first, n, out);
    // A block that cannot move the level (PlateauScan, SIMD on the captured bytes: any layout with a
    // path, any origin and wrap, split history gathered) is accounted for without the scalar path. Only
    // with a known level and no pending pulse: the stream's state stays the scalar path's.
    if (!scalarOnly && level_ >= 0 && !pending_ &&
        acceleration::PlateauScan::quiet(frame, scope::Channel(channel_), first, n, int16_t(low_), int16_t(high_), level_ == 1)) {
        skipped_ += n;
        return 0;
    }
    auto cursor = frame.cursor(scope::Channel(channel_), first);
    uint32_t index = first;
    cursor.run(n, [&](int16_t code) __attribute__((always_inline)) { feed(code, index++, out, count); });
    return count;
}
} // namespace openosc::decoding
