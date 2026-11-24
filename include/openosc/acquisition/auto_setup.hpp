#pragma once
#include "openosc/acquisition/request.hpp"
#include "openosc/acquisition/counter_window.hpp"

namespace openosc::scope {
class AutoSetup {
public:
    enum class State { Idle, Searching, Complete, NoSignal, Clipped, Timeout, ApplyError, Uncalibrated };
    void begin(const ScopeRequest &request, unsigned channel, uint32_t now);
    bool observe(const FrameView &frame,uint32_t now,const CounterSnapshot *counters=nullptr);
    void tick(uint32_t now);
    void fail() { state_ = State::ApplyError; }
    void reset() { state_ = State::Idle; }
    bool active() const { return state_ == State::Searching; }
    State state() const { return state_; }
    const ScopeRequest &request() const { return trial_; }
    const ScopeRequest &original() const { return original_; }
private:
    static constexpr uint64_t SurveyTimebaseNs = 1000000;
    bool vertical(const FrameView &frame,SignalSummary (&summary)[2],uint8_t &usable);
    // Edges of a channel within its band, and the period most of them agree on (if any).
    struct Periodicity { uint32_t edges=0; double hertz=0; bool coherent=false; };
    // Edges without a period from this many on are noise or an aperiodic signal; fewer, a signal
    // slower than the record (EdgeScanner logs a period from every edge after the first). With 2 or
    // more, timing() first waits for FCNT if enabled (aliasing also shows edges without a period).
    static constexpr uint32_t SlowEdges=4, CounterWaitMs=1500;
    static Periodicity periodicity(const FrameView &frame,Channel channel,const SignalBand &band);
    bool timing(const SignalBand &band,const Periodicity &raw,double sampleRate,uint32_t now,const CounterSnapshot *counters);
    // A timebase showing about three periods of `hertz`.
    static uint64_t timebaseFor(double hertz);
    bool changed(uint32_t now);
    void complete();
    // The trigger level for a band: its middle, in the comparator's range.
    static int16_t levelOf(const SignalBand &band);
    ScopeRequest original_, trial_;
    State state_ = State::Idle;
    unsigned channel_ = 0, attempts_ = 0;
    uint32_t deadline_=0,totalDeadline_=0,sequence_=0,appliedAt_=0,counterWaitAt_=0;
    enum class CounterWait : uint8_t { None, Waiting, Done }; // A trial change restarts Waiting; Done holds per search and source.
    CounterWait counterWait_=CounterWait::None;
    uint32_t recordMs() const { return uint32_t(trial_.timebaseNs/100000); } // A trial record's duration.
    // timing_: the level is set for this source; verified_: the level was checked once more on a
    // record at the timebase found, before completing.
    // provisional_: the timebase is FCNT's, taken without a raw period, to be confirmed by one on
    // the next record (else back to fallbackTimebase_); counterRejected_: FCNT was not confirmed
    // for this source and is not taken again.
    bool hasSequence_=false,timing_=false,verified_=false,provisional_=false,counterRejected_=false;
    uint64_t fallbackTimebase_=0;
};
} // namespace openosc::scope
