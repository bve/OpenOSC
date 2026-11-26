#include "openosc/acquisition/counter_window.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
namespace {
// Two rates agree within `relative` of the larger, or three edges of the smaller count. A count
// of none agrees only with none: a filter that removed every edge confirms nothing, and an input
// that appears (or stops) between the unfiltered windows is a change.
bool agree(const CounterSample &a,const CounterSample &b,double relative) {
    if (!a.edges || !b.edges) return !a.edges && !b.edges;
    const double tolerance=std::max(relative,3.0/std::min(a.edges,b.edges));
    return std::abs(a.hertz-b.hertz)<=tolerance*std::max(a.hertz,b.hertz);
}
// An unfiltered window holding more edges than a filtered rate explains: above it, and apart.
bool above(const CounterSample &raw,const CounterSample &filtered) {
    return raw.hertz>filtered.hertz && !agree(raw,filtered,CounterLadder::Agreement);
}
CounterReading reading(const CounterSample &sample,CounterQuality quality,uint8_t filter) {
    CounterReading r;
    r.hertz=sample.hertz; r.edges=sample.edges; r.gateUs=sample.gateUs;
    r.startMs=sample.startMs; r.endMs=sample.endMs;
    r.valid=true; r.quality=quality; r.filter=filter;
    return r;
}
} // namespace
CounterReading CounterLadder::decide(const CounterSample (&w)[Steps],uint32_t kernelHz) {
    const CounterSample &first=w[0],&last=w[Steps-1];
    CounterSample raw; // Both unfiltered windows as one.
    raw.edges=first.edges+last.edges; raw.gateUs=first.gateUs+last.gateUs;
    raw.hertz=raw.gateUs ? raw.edges*1e6/raw.gateUs : 0;
    raw.startMs=first.startMs; raw.endMs=last.endMs;
    if (!raw.edges) { // Only every window empty confirms no input; a filtered window's edges are a change.
        for (unsigned i=1;i+1<Steps;++i) if (w[i].edges) return reading(last,CounterQuality::Changing,0);
        return reading(raw,CounterQuality::Clean,0);
    }
    // Filtered before the unfiltered windows' stability: chatter moves those by any amount (under
    // ADC and render load 12.8 to 14.9 kHz around a fixed 10 kHz 99 % on CH1), and only ever up.
    const double highest=std::max(first.hertz,last.hertz);
    const bool chatter=highest<=ChatterSpread*std::min(first.hertz,last.hertz);
    for (unsigned level=1;chatter && level+1<=3;++level) {
        const CounterSample &a=w[level],&b=w[level+1];
        // Both levels must pass a signal at the unfiltered rate: its half period (50 % duty) at
        // least the stronger level's N samples of f_CK_INT. Beyond that two levels agree on what
        // both lose of a fast signal (60 MHz: ETF 1 and 2 kept 4.4 MHz alike), not on removed chatter.
        if (!kernelHz || 2.0*Samples[Filters[level+1]]*highest>kernelHz) continue;
        if (!a.edges || !b.edges || !agree(a,b,Agreement)) continue;
        CounterSample both; // The two agreeing windows as one: their edges over their time.
        both.edges=a.edges+b.edges; both.gateUs=a.gateUs+b.gateUs; both.hertz=both.edges*1e6/both.gateUs;
        both.startMs=a.startMs; both.endMs=b.endMs;
        if (above(first,both) && above(last,both)) return reading(both,CounterQuality::Filtered,Filters[level]);
    }
    if (!agree(first,last,RawStability)) return reading(last,CounterQuality::Changing,0);
    // Beyond ETRP's guaranteed f_CK_INT/4 a count is no measurement (ST RM0455 for the STM32H7B0, TIM2..5 SMCR, §43.4.3).
    if (kernelHz && 4*raw.hertz>kernelHz) return reading(raw,CounterQuality::Uncertain,0);
    if (agree(w[1],raw,Agreement)) return reading(raw,CounterQuality::Clean,0);
    if (kernelHz && 2*raw.hertz*3>kernelHz) return reading(raw,CounterQuality::Unverified,0);
    return reading(raw,CounterQuality::Uncertain,0);
}
bool CounterWindow::observe(const uint32_t (&counts)[2],uint32_t cycles,uint32_t now) {
    if (!clock_) return false;
    // The time counter is 32-bit. A long foreground stall must not masquerade as a
    // shorter window after one or more complete counter wraps: the cycle starts over.
    if ((uint64_t(uint32_t(now-at_))+1)*clock_>=uint64_t(UINT32_MAX)*1000) {
        begin(counts,cycles,now,clock_,kernelHz_);
        return true;
    }
    const uint32_t elapsed=cycles-cycles_;
    const uint64_t us=uint64_t(elapsed)*1000000/clock_;
    if (us<MinimumUs) return false;
    // A window ends by MaximumUs plus a poll; one far longer (the foreground held up) may hold a
    // wrapped 32-bit count and is not of the moment it would be stated for: the cycle starts over.
    if (us>StaleUs) { begin(counts,cycles,now,clock_,kernelHz_); return true; }
    uint32_t edges[2];
    for (unsigned c=0;c<2;++c) edges[c]=counts[c]-counts_[c];
    if (us<MaximumUs && std::min(edges[0],edges[1])<TargetEdges) return false;
    for (unsigned c=0;c<2;++c) windows_[c][step_]={double(edges[c])*clock_/elapsed,edges[c],uint32_t(us),at_,now};
    if (++step_==CounterLadder::Steps) {
        step_=0;
        for (unsigned c=0;c<2;++c)
            snapshot_.channels[c]=rebased_&(1U<<c) ? CounterReading{} : CounterLadder::decide(windows_[c],kernelHz_);
        rebased_=0;
        ++snapshot_.sequence;
    }
    restart(counts,cycles,now);
    return true;
}
} // namespace openosc::scope
