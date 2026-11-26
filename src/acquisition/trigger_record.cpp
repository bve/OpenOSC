#include "openosc/acquisition/trigger_record.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/acquisition/trigger_qualifier.hpp"
#include <algorithm>

namespace openosc::scope {
namespace {
// When no crossing fits the requested window, associate an Auto event with only the
// samples close to it. Use the 18 us trigger/freeze timing budget and at least one DMA group.
uint32_t autoEventRadius(const FrameMetadata &m, uint32_t reach) {
    constexpr uint32_t MatchNs = 18000;
    const auto &rate = m.samplesPerSecond;
    const uint64_t denominator = uint64_t(rate.denominator) * 1000000000;
    const uint32_t samples = uint32_t((uint64_t(rate.numerator) * MatchNs + denominator - 1) / denominator);
    return std::max<uint32_t>(RecordDepth::Granule * pointsPerWord(m.layout), std::min(samples, reach));
}
// The nearest edge to target at indices from..to (none when from > to); ties to the lower index.
bool nearestIn(const FrameView &ring,const TriggerSettings &settings,uint32_t target,uint32_t from,uint32_t to,
               bool &found,uint32_t &index) {
    uint32_t at=0;
    if (from>to || !TriggerSearch::nearest(ring,settings,target,from,to,at)) return false;
    const auto distance=[target](uint32_t i) { return i>target ? i-target : target-i; };
    if (!found || distance(at)<distance(index) || (distance(at)==distance(index) && at<index)) index=at;
    found=true;
    return true;
}
} // namespace
FrameView TriggerRecord::select(const FrameView &ring, const TriggerSettings &settings, uint32_t count, uint32_t target,
                               bool forced, bool comparatorEvent, bool untriggered) {
    if (!ring.valid() || ring.metadata().rolling || !settings.valid() || count<RecordDepth::Minimum || count>RecordDepth::Maximum || ring.metadata().samples<4) return {};
    const uint32_t samples=ring.metadata().samples;
    count=std::min(count,samples);
    target=std::min(target,samples-1);
    const uint32_t pre=uint64_t(count-1)*settings.position/1000;
    const bool automatic = settings.mode == TriggerMode::Auto && !forced;
    // The edges the record fits around at its pretrigger point (pre .. samples-count+pre), from no
    // earlier than the gate's slack before the event: not an older burst out of a long wait.
    const uint32_t reach=count/SlackDivisor;
    const uint32_t lowest=std::max<uint32_t>({pre,1U,target>reach ? target-reach : 0U});
    const uint32_t fitEnd = samples - count + pre;
    const uint32_t highest = automatic && comparatorEvent ? std::min(fitEnd, target + reach) : fitEnd;
    uint32_t index=0;
    if (settings.qualified()) { // One pass for both windows: the record's fit and, for a fallback, the whole ring.
        TriggerQualifier::Window windows[2] = {{lowest, highest}, {1, samples - 1}};
        TriggerQualifier::nearest(ring, settings, target, windows, automatic ? 1 : 2);
        if (windows[0].found) {
            index = windows[0].index;
            return ring.slice(index - pre, count).withTrigger(true, pre, settings.tag()).withPretrigger(pre);
        }
        // Auto: a comparator event no candidate qualified at is not the trigger. The capture rearms
        // (nothing returned) until `untriggered` says Auto's wait is over; its timeout always shows.
        if (automatic) return comparatorEvent && !untriggered ? FrameView{}
                                                              : ring.slice(samples - count, count).withTrigger(false, 0).withPretrigger(pre);
        if (!windows[1].found && !forced) return {};
        const uint32_t at = windows[1].found ? windows[1].index : target;
        const uint32_t first = std::min(at > pre ? at - pre : 0, samples - count);
        if (!windows[1].found) return ring.slice(first, count).withTrigger(false, 0).withPretrigger(pre);
        return ring.slice(first, count).withShiftedTrigger(at - first, int32_t(int64_t(at - first) - pre), settings.tag()).withPretrigger(pre);
    }
    // The two ADCs can resolve ringing around one event into short recrossings. Prefer a
    // crossing supported by both ADCs on each side, close to the comparator's DMA cursor.
    // The ordinary search still accepts isolated/narrow pulses when that stronger edge is absent.
    if (ring.metadata().layout == Layout::InterleavedCh1 && comparatorEvent) {
        constexpr uint32_t Nearby = 8;
        const uint32_t first = std::max(lowest, target > Nearby ? target - Nearby : 0);
        const uint32_t last = std::min(highest, target + Nearby);
        if (TriggerSearch::nearestStablePair(ring, settings, target, first, last, index))
            return ring.slice(index - pre, count).withTrigger(true, pre, settings.tag()).withPretrigger(pre);
    }
    bool found=false;
    if (nearestIn(ring,settings,target,lowest,highest,found,index))
        return ring.slice(index-pre,count).withTrigger(true,pre,settings.tag()).withPretrigger(pre);
    // Normal, Single and a manual force can still use an edge in the rest of the record.
    if (automatic) {
        const uint32_t radius = autoEventRadius(ring.metadata(), reach);
        if (!comparatorEvent ||
            !TriggerSearch::nearest(ring, settings, target, target > radius ? target - radius : 1, target + radius, index))
            return ring.slice(samples-count,count).withTrigger(false,0).withPretrigger(pre); // Marked there (#65).
        found = true;
    } else if (lowest>highest) nearestIn(ring,settings,target,1,samples-1,found,index);
    else {
        nearestIn(ring,settings,target,1,lowest-1,found,index);
        nearestIn(ring,settings,target,highest+1,samples-1,found,index);
    }
    if (!found && !forced) return {};
    const uint32_t at=found ? index : target;
    const uint32_t first=std::min(at>pre ? at-pre : 0,samples-count);
    if (!found) return ring.slice(first,count).withTrigger(false,0).withPretrigger(pre);
    return ring.slice(first,count).withShiftedTrigger(index-first,int32_t(int64_t(index-first)-pre),settings.tag()).withPretrigger(pre);
}
} // namespace openosc::scope
