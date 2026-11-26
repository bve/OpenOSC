#include "openosc/acquisition/reciprocal_counter.hpp"
#include <algorithm>

namespace openosc::scope {
bool ReciprocalPolicy::configure(uint32_t kernelHz) {
    reset();
    kernelHz_=kernelHz; ticksPerMs_=kernelHz/1000;
    // The longest legitimate window: a gap of the wait limit after WindowMs, polled StallMs late,
    // must stay under the wrap (11.9 s at 360 MHz).
    const uint64_t wrapMs=ticksPerMs_ ? (uint64_t(1)<<32)/ticksPerMs_ : 0;
    const uint64_t margin=StallMs+WindowMs+50;
    const uint64_t limitMs=wrapMs>margin ? std::min<uint64_t>(MaximumWaitMs,wrapMs-margin) : 0;
    usable_=kernelHz%1000000==0 && limitMs>=1000;
    windowTicks_=ticksPerMs_*WindowMs;
    limitTicks_=uint32_t(ticksPerMs_*limitMs);
    minimumWaitTicks_=ticksPerMs_*MinimumWaitMs;
    fastTicks_=uint32_t(kernelHz/FastHz);
    return usable_;
}
void ReciprocalPolicy::reset() {
    timing_=pending_=holding_=uncertain_=false;
    periodTicks_=0;
    forget();
}
CounterReading ReciprocalPolicy::gate(const CounterReading &reading,uint32_t nowMs) {
    if (pending_) {
        if (!reading.valid) return held_; // The cycle spanning the hand-back.
        pending_=false;
    }
    if (uncertain_ && !before(nowMs,uncertainUntilMs_)) uncertain_=false;
    CounterReading r=reading;
    // A count of some edges during the chatter hold-off; none, or a changing input, as it is.
    if (uncertain_ && r.valid && r.edges && r.quality!=CounterQuality::Changing) r.quality=CounterQuality::Uncertain;
    return r;
}
bool ReciprocalPolicy::enters(const CounterReading &gate,uint32_t nowMs) {
    if (!usable_ || !enabled_ || timing_ || pending_ || !gate.valid) return false;
    if (holding_ && before(nowMs,holdUntilMs_)) return false;
    holding_=false;
    if (gate.quality==CounterQuality::Clean) return gate.edges && gate.hertz<EnterHz;
    return (gate.quality==CounterQuality::Changing || gate.quality==CounterQuality::Uncertain) && gate.hertz<SlowHz;
}
void ReciprocalPolicy::enter(const CounterReading &gate,uint32_t nowTicks,uint32_t nowMs) {
    timing_=true;
    // A Clean rate gives the first wait; a Changing or Uncertain one is no period.
    periodTicks_=gate.quality==CounterQuality::Clean && gate.hertz>0 ? uint32_t(std::min<double>(kernelHz_/gate.hertz,limitTicks_)) : 0;
    restart(nowTicks,nowMs);
}
void ReciprocalPolicy::restart(uint32_t nowTicks,uint32_t nowMs) { waitFrom_=nowTicks; polledMs_=nowMs; }
uint32_t ReciprocalPolicy::waitTicks() const {
    if (!periodTicks_) return limitTicks_;
    return uint32_t(std::clamp<uint64_t>(2*uint64_t(periodTicks_),minimumWaitTicks_,limitTicks_));
}
ReciprocalPolicy::Event ReciprocalPolicy::step(const EdgeWindow &w,uint32_t nowMs) {
    polledMs_=nowMs;
    if (w.tripped || w.overrun) return Event::Storm;
    if (w.ended) {
        const uint32_t span=w.last-w.first,periods=w.count-1;
        if (periods>=2 && uint64_t(w.shortest)*ChatterRatio<w.longest) return Event::Chatter;
        CounterReading r;
        r.valid=r.timed=true; r.quality=CounterQuality::Clean; r.filter=CaptureFilter;
        r.edges=periods; r.gateUs=uint32_t(uint64_t(span)*1000000/kernelHz_);
        r.hertz=double(periods)*kernelHz_/span;
        r.endMs=nowMs-(w.now-w.last)/ticksPerMs_; r.startMs=r.endMs-span/ticksPerMs_;
        reading_=r;
        periodTicks_=span/periods;
        return r.hertz>ReturnHz ? Event::Fast : Event::Reading;
    }
    if (w.pending) return Event::None; // An edge came: the interrupt has yet to log it.
    const uint32_t since=w.now-(w.count ? w.last : waitFrom_);
    return since>waitTicks() ? Event::Missing : Event::None;
}
void ReciprocalPolicy::leave(Event why,uint32_t nowMs) {
    timing_=false; pending_=true;
    periodTicks_=0;
    held_=why==Event::Fast ? reading_ : CounterReading{};
    if (why==Event::Chatter || why==Event::Storm) { holding_=true; holdUntilMs_=nowMs+HoldoffMs; }
    if (why==Event::Chatter) { uncertain_=true; uncertainUntilMs_=nowMs+HoldoffMs; }
}
} // namespace openosc::scope
