#include "openosc/acquisition/analysis.hpp"

namespace openosc::scope {
namespace {
uint8_t enabledChannels(const FrameMetadata &m) { return uint8_t(m.channels[0].enabled) | uint8_t(m.channels[1].enabled) << 1; }
} // namespace
bool TriggerCache::matches(const FrameView &frame, const TriggerSettings &s) const {
    const auto &m = frame.metadata();
    return valid_ && timestamp_ == m.timestampUs && sequence_ == m.sequence && samples_ == m.samples &&
           origin_ == m.origin && ring_ == m.ring && layout_ == m.layout && channels_ == enabledChannels(m) &&
           settings_.sameSearch(s) && settings_.mode == s.mode;
}
void TriggerCache::remember(const FrameView &frame, const TriggerSettings &s, bool found, uint32_t index) {
    if (!frame.valid() || !s.valid()) { valid_ = false; return; }
    const auto &m = frame.metadata();
    timestamp_ = m.timestampUs; sequence_ = m.sequence; samples_ = m.samples; origin_ = m.origin; ring_ = m.ring;
    layout_ = m.layout; channels_ = enabledChannels(m); settings_ = s;
    found_ = found; index_ = found ? index : 0; valid_ = true;
}
bool TriggerCache::find(const FrameView &frame, const TriggerSettings &s, uint32_t &index) {
    if (!frame.valid() || !s.valid())
        return false;
    if (!matches(frame, s)) {
        uint32_t at = 0;
        const bool found = TriggerSearch::find(frame, s, at);
        remember(frame, s, found, at);
    }
    if (found_)
        index = index_;
    return found_;
}
} // namespace openosc::scope
