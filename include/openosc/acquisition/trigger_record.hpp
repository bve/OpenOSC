#pragma once
#include "openosc/acquisition/analysis.hpp"
#include <algorithm>

namespace openosc::scope {
class TriggerRecord {
public:
    // The record around an edge, the qualified crossing nearest `target` (the comparator event) that
    // the ring holds with the whole record around it at the configured pretrigger, at most
    // count / SlackDivisor before the event: the edge is then the record's point
    // (count - 1) * position / 1000 exactly. The comparator fires on the first raw
    // crossing it sees, which need not be such an edge (Noise reject, comparator and ADC thresholds
    // apart): the nearest edge it does not fit is taken only when none fits, the record then cut at the
    // ring's end and its FrameMetadata::triggerShift saying how far its edge is from the pretrigger.
    // At a comparator event in a paired capture, prefer a nearby edge with two samples on each side.
    // Without an edge: Auto takes the newest `count` points, a forced capture the record placed at
    // `target` as an edge would be (it is not marked triggered), Normal/Single nothing.
    static FrameView select(const FrameView &ring, const TriggerSettings &settings, uint32_t count, uint32_t target,
                            bool forced = false, bool comparatorEvent = false);
    // Points the gate adds before arming and after the post-trigger part of a `count` record in a
    // ring of `capacity` whose `reserved` points are the late freeze's (SamplePlan::headroom): the
    // history select() needs to find an edge that fits on either side of the event. At most
    // count / SlackDivisor each (the time it adds to a capture), within half the spare ring.
    static constexpr uint32_t SlackDivisor = 32;
    static constexpr uint32_t slack(uint32_t count, uint32_t capacity, uint32_t reserved) {
        const uint64_t used = uint64_t(count) + reserved;
        return capacity > used ? std::min<uint32_t>(count / SlackDivisor, uint32_t((capacity - used) / 2)) : 0;
    }
};
} // namespace openosc::scope
