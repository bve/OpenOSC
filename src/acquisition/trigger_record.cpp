#include "openosc/acquisition/trigger_record.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include <algorithm>

namespace openosc::scope {
namespace {
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
FrameView TriggerRecord::select(const FrameView &ring,const TriggerSettings &settings,uint32_t count,uint32_t target,bool forced) {
    if (!ring.valid() || ring.metadata().rolling || !settings.valid() || count<RecordDepth::Minimum || count>RecordDepth::Maximum || ring.metadata().samples<4) return {};
    const uint32_t samples=ring.metadata().samples;
    count=std::min(count,samples);
    target=std::min(target,samples-1);
    const uint32_t pre=uint64_t(count-1)*settings.position/1000;
    // The edges the record fits around at its pretrigger point (pre .. samples-count+pre), from no
    // earlier than the gate's slack before the event: not an older burst out of a long wait.
    const uint32_t reach=count/SlackDivisor;
    const uint32_t lowest=std::max<uint32_t>({pre,1U,target>reach ? target-reach : 0U}), highest=samples-count+pre;
    uint32_t index=0;
    bool found=false;
    if (nearestIn(ring,settings,target,lowest,highest,found,index))
        return ring.slice(index-pre,count).withTrigger(true,pre,settings.tag());
    // None fits: the nearest one of the rest (with the range above, a single pass over the ring).
    if (lowest>highest) nearestIn(ring,settings,target,1,samples-1,found,index);
    else {
        nearestIn(ring,settings,target,1,lowest-1,found,index);
        nearestIn(ring,settings,target,highest+1,samples-1,found,index);
    }
    if (settings.mode!=TriggerMode::Auto && !found && !forced) return {};
    if (!found && !forced) return ring.slice(samples-count,count).withTrigger(false,0);
    const uint32_t at=found ? index : target;
    const uint32_t first=std::min(at>pre ? at-pre : 0,samples-count);
    if (!found) return ring.slice(first,count).withTrigger(false,0);
    return ring.slice(first,count).withShiftedTrigger(index-first,int32_t(int64_t(index-first)-pre),settings.tag());
}
} // namespace openosc::scope
