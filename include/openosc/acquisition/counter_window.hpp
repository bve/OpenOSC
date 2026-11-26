#pragma once
#include <cstdint>

namespace openosc::scope {
// What a stated hardware count is (CounterLadder): Clean, the unfiltered edge rate that the
// weakest filter confirms; Filtered, the count at `filter`, where two neighbouring filter
// levels agree below the unfiltered rate (comparator chatter or narrow pulses removed: which of
// the two is not knowable from counts); Unverified, the unfiltered rate of a signal whose half
// period is shorter than the weakest filter's window, so no filter can check it; Uncertain, the
// unfiltered rate that nothing confirms or explains (no agreeing levels: narrow pulses with
// chatter, mixed widths); Changing, the latest unfiltered rate of a cycle whose two unfiltered
// windows disagree (the input changed).
enum class CounterQuality : uint8_t { Clean, Filtered, Unverified, Uncertain, Changing };
struct CounterReading {
    double hertz=0;
    uint32_t edges=0,gateUs=0; // Of the windows the stated rate comes from (gateUs: their total).
    bool valid=false;
    uint32_t endMs=0;          // When the last of those windows ended (ms of observe()'s clock).
    uint32_t startMs=0;        // When the first of them began: [startMs, endMs] spans the count.
    CounterQuality quality=CounterQuality::Clean;
    uint8_t filter=0;          // The ETR filter level (ETF) of the stated count, 0: unfiltered.
    // A reciprocal count of CH1 (ReciprocalPolicy): `edges` periods between the first and the last
    // captured rising edge over `gateUs`, through the input capture filter (filter 3: N = 8).
    bool timed=false;
    // A count to act on (AUTO): clean, filtered or unverified by bandwidth; not uncertain or changing.
    bool settled() const { return valid && quality!=CounterQuality::Uncertain && quality!=CounterQuality::Changing; }
};
static_assert(sizeof(CounterReading)==32,"`timed` stays in the padding");
// `sequence` changes with any channel's reading, `updates[c]` with channel c's: a timed CH1 renews
// its reading apart from CH2's gate cycles.
struct CounterSnapshot {
    CounterReading channels[2];
    uint32_t sequence=0;
    uint32_t updates[2]{};
    uint8_t enabled=3;
};
// One window's count of one channel at one filter level.
struct CounterSample {
    double hertz=0;
    uint32_t edges=0,gateUs=0,startMs=0,endMs=0;
};
// The decision of one channel over one cycle of windows at the ETR filter levels Filters: an
// unfiltered window, ETF 1, 2 and 3 (N = 2, 4 and 8 samples at f_CK_INT), and an unfiltered one
// again. Each cycle starts and ends unfiltered, so no filter level can hold the count at zero or
// at a subharmonic. Rates are compared, not edges (the windows differ in length). In order:
//   (none):   no edge in any window: a valid 0, Clean; none unfiltered but some filtered (a burst
//             the unfiltered windows missed): Changing, 0;
//   Filtered: the weakest level L of 1, 2 whose rate and level L+1's agree (Agreement or 3
//             edges, both counted some), with each unfiltered window above that rate, the two
//             within ChatterSpread of each other, and the higher unfiltered rate within both
//             levels' band (2 x N(L+1) x rate <= f_CK_INT: a half period of N samples at 50 %
//             duty; beyond it both levels lose a fast signal's edges alike, and their agreement is
//             attenuation, not removed chatter): the count of those two windows as one
//             ([startMs, endMs] theirs). Chatter only adds edges and varies (a fixed 10 kHz 99 %
//             read 12.8 to 14.9 kHz unfiltered on CH1 under ADC and render load, a ratio of 1.16;
//             under 1.01 in HOLD), so RawStability does not apply to it. An input that changes
//             inside the cycle breaks the levels' agreement, leaves an unfiltered window at or
//             below their rate (a slower one) or far above the other (a faster one);
//   Changing: the unfiltered windows differ by more than RawStability (2 %) or 3 edges, or
//             only one of them counted any (a count of none agrees only with none);
//   Uncertain: an unfiltered rate above f_CK_INT/4, ETRP's guaranteed limit (70 MHz at 280 MHz);
//   Clean:    ETF 1 within the agreement of the unfiltered rate;
//   Unverified: a half period of the unfiltered rate shorter than ETF 1's window plus a sample
//             (2 x rate x 3 > f_CK_INT: about 47 to 70 MHz at 280 MHz, where ETF 1 loses edges);
//   Uncertain: anything else (the unfiltered rate, marked).
// A filter level that counted none never confirms. The unfiltered rate spans the whole cycle.
class CounterLadder {
public:
    static constexpr unsigned Steps=5;
    static constexpr uint8_t Filters[Steps]={0,1,2,3,0};
    static constexpr uint8_t Samples[4]={1,2,4,8}; // N of each ETF level at f_CK_INT (RM0455 TIMx_SMCR).
    static constexpr double RawStability=.02,Agreement=2e-4,ChatterSpread=1.5;
    static CounterReading decide(const CounterSample (&windows)[Steps],uint32_t kernelHz);
};
// The counter windows of both channels, on the reference timer (clock ticks per second): a window
// lasts at least MinimumUs and ends at MaximumUs or once both channels counted TargetEdges, so a
// cycle takes 0.1 s for fast signals and 1 s for slow ones (or none). After observe() reports the
// end of a window the owner sets filter() on both counters and restart()s the window from counts
// read after that change: a window never spans a filter change. A completed cycle updates the
// snapshot of both channels (CounterLadder); begin() starts a cycle over with no reading.
class CounterWindow {
public:
    static constexpr uint32_t MinimumUs=20000,MaximumUs=200000,TargetEdges=2000;
    // A window longer than this (polls held up) starts the cycle over without a reading.
    static constexpr uint32_t StaleUs=2*MaximumUs;
    void begin(const uint32_t (&counts)[2],uint32_t cycles,uint32_t now,uint32_t clock,uint32_t kernelHz=0) {
        clock_=clock; kernelHz_=kernelHz; step_=0; rebased_=0;
        restart(counts,cycles,now);
        const uint32_t next=snapshot_.sequence+1;
        snapshot_={}; snapshot_.sequence=next;
    }
    void restart(const uint32_t (&counts)[2],uint32_t cycles,uint32_t now) {
        cycles_=cycles; at_=now;
        for (unsigned c=0;c<2;++c) counts_[c]=counts[c];
    }
    // Channel c's count continues from another counter (CH1 between its gate and its edge capture):
    // the other channel's windows go on, and the cycle under way states no reading of channel c.
    void rebase(unsigned channel,uint32_t count) { counts_[channel]=count; rebased_|=uint8_t(1U<<channel); }
    // True when a window ended (or a stall started the cycle over): set filter(), then restart().
    bool observe(const uint32_t (&counts)[2],uint32_t cycles,uint32_t now);
    uint8_t filter() const { return CounterLadder::Filters[step_]; }
    const CounterSnapshot &snapshot() const { return snapshot_; }
    // With IRQ masked, a pending UIF means the software high word has not
    // incorporated this rollover. Re-read low after observing that flag.
    static uint32_t extend16(uint32_t high,uint16_t low,bool pending,uint16_t after) {
        return ((high+unsigned(pending))<<16)|(pending ? after : low);
    }
private:
    CounterSnapshot snapshot_;
    CounterSample windows_[2][CounterLadder::Steps];
    uint32_t counts_[2]{},clock_=0,kernelHz_=0,cycles_=0,at_=0;
    unsigned step_=0;
    uint8_t rebased_=0; // Channels whose cycle under way spans a rebase().
};
} // namespace openosc::scope
