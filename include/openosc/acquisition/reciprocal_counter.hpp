#pragma once
#include "openosc/acquisition/counter_window.hpp"
#include <cstdint>

namespace openosc::scope {
// What the capture interrupt logged of one timed window, taken with the interrupt masked.
struct EdgeWindow {
    uint32_t count=0;           // Rising edges in the window, its first included.
    uint32_t first=0,last=0;    // Their capture stamps (timer ticks).
    uint32_t shortest=0,longest=0; // Of the count - 1 gaps between them.
    uint32_t now=0;             // The timer when taken.
    bool ended=false;           // The window was complete and the log continues from `last`.
    bool pending=false;         // A captured edge waits for the interrupt.
    bool tripped=false;         // FastLimit edges came faster than the fast gap: the interrupt is off.
    bool overrun=false;         // An edge was captured over one not yet read (an edge was lost).
};
// CH1's rising edges as TIM2's input capture interrupt logs them. record() runs in that interrupt:
// integer loads, compares and stores only. The foreground calls everything else with the
// interrupt masked (CriticalSection), so a window is taken and continued atomically.
class EdgeLog {
public:
    static constexpr uint32_t FastLimit=1000;
    void arm(uint32_t fastTicks) { fastTicks_=fastTicks; total_=0; tripped_=overrun_=false; restart(); }
    // The next edge starts a window: what was logged since the last window is dropped.
    void restart() { count_=0; shortest_=UINT32_MAX; longest_=0; fast_=0; }
    // One captured edge. False once the window holds FastLimit gaps shorter than the fast gap:
    // the caller disables the interrupt.
    bool record(uint32_t stamp) {
        ++total_;
        if (count_++) {
            const uint32_t gap=stamp-last_;
            if (gap<shortest_) shortest_=gap;
            if (gap>longest_) longest_=gap;
            if (gap<fastTicks_ && ++fast_>=FastLimit) tripped_=true;
        } else first_=stamp;
        last_=stamp;
        return !tripped_;
    }
    void overrun() { overrun_=true; }
    // The window so far; once it holds two edges `minimumTicks` apart it ends, and the next one
    // starts at its last edge.
    EdgeWindow take(uint32_t now,bool pending,uint32_t minimumTicks) {
        EdgeWindow w;
        w.count=count_; w.first=first_; w.last=last_; w.shortest=shortest_; w.longest=longest_;
        w.now=now; w.pending=pending; w.tripped=tripped_; w.overrun=overrun_;
        if (count_>=2 && last_-first_>=minimumTicks) {
            w.ended=true;
            first_=last_; count_=1; shortest_=UINT32_MAX; longest_=0; fast_=0;
        }
        return w;
    }
    uint32_t total() const { return total_; } // Every edge logged since arm(): CH1's gate count while timed.
private:
    uint32_t total_=0,count_=0,first_=0,last_=0,shortest_=UINT32_MAX,longest_=0,fast_=0,fastTicks_=0;
    bool tripped_=false,overrun_=false;
};
// When CH1 is timed instead of counted (GitHub #173), and what it states. Foreground only.
// - Enters from a completed gate cycle of CH1: a Clean count of some edges under EnterHz, or a
//   Changing or Uncertain one under SlowHz (all the gate gives for a slow signal), not within a
//   hold-off.
// - Times windows of at least WindowMs: (edges - 1) periods between the first and the last edge, a
//   window's last edge the next one's first. A rate above ReturnHz is stated, then the gate takes over.
// - Waits for an edge two periods of the last timed (or Clean gate) rate, at least MinimumWaitMs
//   and at most the wait limit (MaximumWaitMs, under the 32-bit tick counter's wrap); from a Changing
//   or Uncertain entry, the wait limit. An edge that never comes is Missing: no reading.
// - Chatter (shortest gap under 1/ChatterRatio of the longest in a window) hands back to the gate
//   for HoldoffMs with its readings Uncertain; a storm (the interrupt's fast guard or an
//   overcapture) does the same without reading or marking.
// - After a hand-back CH1 states no reading (a Fast one: its last timed reading) until the gate's
//   first complete cycle.
class ReciprocalPolicy {
public:
    static constexpr double EnterHz=5000,SlowHz=12,ReturnHz=20000,FastHz=100000;
    static constexpr uint32_t WindowMs=250,MaximumWaitMs=10000,MinimumWaitMs=20,HoldoffMs=10000;
    // Polls further apart than this may have seen the tick counter wrap: the window starts over.
    static constexpr uint32_t StallMs=1500;
    static constexpr uint32_t ChatterRatio=8;
    static constexpr uint8_t CaptureFilter=3; // IC1F: N = 8 samples at f_CK_INT, as ETF 3.
    enum class Event : uint8_t { None, Reading, Fast, Missing, Chatter, Storm, Disabled };
    // The timer's clock, a whole number of MHz. False: its wrap leaves no wait limit (never timed).
    bool configure(uint32_t kernelHz);
    void enable(bool on) { enabled_=on; }
    bool enabled() const { return enabled_; }
    bool timing() const { return timing_; }
    // What CH1 states for a completed gate cycle (after a hand-back: what it keeps, or the gate's
    // reading marked during the hold-off).
    CounterReading gate(const CounterReading &reading,uint32_t nowMs);
    bool enters(const CounterReading &gate,uint32_t nowMs);
    void enter(const CounterReading &gate,uint32_t nowTicks,uint32_t nowMs);
    // The window starts over (a stall or a new configuration): waiting from now.
    void restart(uint32_t nowTicks,uint32_t nowMs);
    bool stalled(uint32_t nowMs) const { return uint32_t(nowMs-polledMs_)>StallMs; }
    Event step(const EdgeWindow &window,uint32_t nowMs);
    const CounterReading &reading() const { return reading_; }
    void leave(Event why,uint32_t nowMs);
    const CounterReading &stated() const { return held_; } // CH1 after leave().
    // No reading kept (the counters were invalidated).
    void forget() { reading_=held_=CounterReading{}; }
    void reset(); // The counters stopped: gate, no hold-off, nothing kept.
    uint32_t windowTicks() const { return windowTicks_; }
    uint32_t fastTicks() const { return fastTicks_; }
    uint32_t waitTicks() const;
private:
    static bool before(uint32_t nowMs,uint32_t untilMs) { return int32_t(nowMs-untilMs)<0; }
    uint32_t kernelHz_=0,ticksPerMs_=0,windowTicks_=0,limitTicks_=0,minimumWaitTicks_=0,fastTicks_=0;
    uint32_t periodTicks_=0; // The expected period, 0: unknown (the wait limit applies).
    uint32_t waitFrom_=0,polledMs_=0,holdUntilMs_=0,uncertainUntilMs_=0;
    bool usable_=false,enabled_=false,timing_=false,pending_=false,holding_=false,uncertain_=false;
    CounterReading reading_,held_;
};
} // namespace openosc::scope
