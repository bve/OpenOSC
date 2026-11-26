#include "openosc/acquisition/trigger_qualifier.hpp"
#include <algorithm>

namespace openosc::scope {
TriggerQualifier::Limit TriggerQualifier::limit(uint32_t ns, Rational rate) {
    if (!rate.valid()) return {};
    // ns < 2^30 and the numerator < 2^32: the product and the divisor both fit 64 bits.
    const uint64_t product = uint64_t(ns) * rate.numerator, divisor = uint64_t(rate.denominator) * 1000000000ULL;
    return {product / divisor, product % divisor != 0};
}
TriggerQualifier::TriggerQualifier(const TriggerSettings &s, Rational rate)
    : type_(s.type), condition_(s.condition) {
    const bool window = s.condition == TriggerCondition::Inside || s.condition == TriggerCondition::Outside;
    minimum_ = limit(window ? std::min(s.minimumNs, s.maximumNs) : s.minimumNs, rate);
    maximum_ = limit(window ? std::max(s.minimumNs, s.maximumNs) : s.maximumNs, rate);
    const bool rising = s.edge == Edge::Rising;
    const int low = s.type == TriggerType::Width ? s.level : s.lowLevel();
    const int high = s.type == TriggerType::Width ? s.level : s.highLevel();
    sign_ = rising ? 1 : -1;
    low_ = rising ? low : -high;
    high_ = rising ? high : -low;
    rail_ = rising ? 127 : 128;
    const int lowest = rising ? -128 : -127;
    arm_ = std::max(lowest, low_ - (s.reject ? TriggerSettings::RejectCodes : 0));
}
bool TriggerQualifier::qualifies(uint64_t points) const {
    switch (condition_) {
    case TriggerCondition::Any: return true;
    case TriggerCondition::Less: return maximum_.below(points);
    case TriggerCondition::Greater: return minimum_.above(points);
    case TriggerCondition::Inside: return minimum_.above(points) && maximum_.below(points);
    case TriggerCondition::Outside: return minimum_.below(points) || maximum_.above(points);
    }
    return false;
}
bool TriggerQualifier::feed(int16_t code) {
    const int o = sign_ * code;
    const uint32_t i = at_++;
    if (type_ == TriggerType::Transition) {
        switch (phase_) {
        case Phase::Idle:
            if (o <= arm_) { armed_ = true; return false; }
            if (o <= low_ || !armed_) return false;
            armed_ = false; start_ = i; phase_ = Phase::Inside;
            break;
        case Phase::Ending: // Back at or below the low level: chatter until the signal is armed again.
            if (o <= arm_) { phase_ = Phase::Idle; armed_ = true; return false; }
            if (o <= low_) return false;
            phase_ = Phase::Inside;
            break;
        case Phase::Inside:
            if (o <= low_) { phase_ = o <= arm_ ? Phase::Idle : Phase::Ending; armed_ = o <= arm_; return false; }
            break;
        case Phase::Done:
            if (o <= arm_) { phase_ = Phase::Idle; armed_ = true; }
            return false;
        }
        if (!past(o)) return false;
        phase_ = Phase::Done; end_ = i;
        return qualifies(i - start_);
    }
    switch (phase_) {
    case Phase::Idle:
        if (o <= arm_) { armed_ = true; return false; }
        if (o <= low_) return false;
        if (armed_) { start_ = i; reached_ = past(o); phase_ = Phase::Inside; }
        armed_ = false;
        return false;
    case Phase::Inside:
        if (o > low_) { reached_ = reached_ || past(o); return false; }
        end_ = i; phase_ = Phase::Ending;
        break;
    default: // Ending: back over the level, decided once the signal is armed again.
        if (o > low_) { reached_ = reached_ || past(o); phase_ = Phase::Inside; return false; }
        break;
    }
    if (o > arm_) return false;
    phase_ = Phase::Idle; armed_ = true;
    return (type_ != TriggerType::Runt || !reached_) && qualifies(end_ - start_);
}
void TriggerQualifier::nearest(const FrameView &frame, const TriggerSettings &settings, uint32_t target, Window *windows,
                               unsigned count) {
    for (unsigned w = 0; w < count; ++w) windows[w].found = false;
    if (!frame.valid() || !settings.valid() || !settings.qualified() || frame.metadata().samples < 2) return;
    const uint32_t samples = frame.metadata().samples;
    constexpr unsigned Most = 4;
    bool open[Most]{};
    unsigned remaining = 0;
    uint32_t reach = 0; // The highest last index of a window still open.
    for (unsigned w = 0; w < count && w < Most; ++w) {
        auto &window = windows[w];
        window.first = std::max<uint32_t>(window.first, 1);
        window.last = std::min(window.last, samples - 1);
        if ((open[w] = window.first <= window.last)) { ++remaining; reach = std::max(reach, window.last); }
    }
    TriggerQualifier qualifier(settings, frame.metadata().samplesPerSecond);
    const auto offer = [&](uint32_t point) {
        for (unsigned w = 0; w < count && w < Most; ++w) {
            if (!open[w]) continue;
            auto &window = windows[w];
            if (point > window.last) { open[w] = false; --remaining; continue; }
            if (point < window.first) continue;
            const uint32_t distance = point > target ? point - target : target - point;
            const uint32_t best = window.index > target ? window.index - target : target - window.index;
            if (!window.found || distance < best) { window.index = point; window.found = true; } // Later ties are higher.
            if (point >= target) { open[w] = false; --remaining; } // Every later point is farther.
        }
    };
    auto cursor = frame.cursor(settings.channel);
    constexpr uint32_t Chunk = 256;
    for (uint32_t at = 0; remaining && at < samples;) {
        const uint32_t n = cursor.run(Chunk, [&](int16_t code) __attribute__((always_inline)) {
            if (qualifier.feed(code)) offer(qualifier.point());
        });
        if (!n) break;
        at += n;
        if (at > reach + 1 && !qualifier.pending()) break; // No point at or below any window's end is to come.
    }
}
bool TriggerQualifier::find(const FrameView &frame, const TriggerSettings &settings, uint32_t &index) {
    Window window{1, UINT32_MAX};
    nearest(frame, settings, 0, &window, 1);
    if (window.found) index = window.index;
    return window.found;
}
} // namespace openosc::scope
