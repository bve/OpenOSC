#pragma once
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/trigger_delay_plan.hpp"
#include "openosc/platform/resources.hpp"

namespace openosc::scope {
class TriggerSink {
public:
    virtual ~TriggerSink()=default;
    virtual void triggerEvent(bool external)=0;
    virtual void freezeRecord()=0;
};
// Owns comparator EXTI0/15 and the TIM14 prefill/Auto/post-trigger delay.
class TriggerGate {
public:
    explicit TriggerGate(PeripheralResources &resources) : resources_(resources) {}
    ~TriggerGate() { stop(); releaseLines(); }
    // prepare() does everything that can fail (checks, delay plan, the TIM14/EXTI lease) and, before
    // the capture's DMA runs, every EXTI/SYSCFG/RCC write (configureLines); begin() then arms the
    // prefill delay, cannot fail and touches only TIM14 and the NVIC. start() is both, for callers with no DMA to order.
    // `settleMs` lengthens the prefill: no trigger, Auto timeout or force before it has passed.
    // `slack` points (TriggerRecord::slack) lengthen both the prefill and the post-trigger delay: the
    // ring then holds them on each side of the event for the edge the record is cut at.
    bool prepare(const TriggerSettings &settings,Rational rate,uint32_t samples,uint32_t settleMs=0,uint32_t slack=0);
    void begin(TriggerSink &sink);
    bool start(const TriggerSettings &settings,Rational rate,uint32_t samples,TriggerSink &sink) {
        if (!prepare(settings,rate,samples)) return false;
        begin(sink); return true;
    }
    // No SRD access (GitHub #33): the EXTI line, and with it the TIM14/EXTI lease, is kept for
    // releaseLines(). prepare() is refused until then; begin() needs a new prepare().
    void stop();
    // The EXTI line masked, its edges off and its pending bit cleared, then the lease released: only
    // with no capture DMA running (CaptureDma::quiesce once every stream is stopped), and only for a
    // stopped gate (not running, not prepared). Callers with no DMA: stop() then releaseLines().
    void releaseLines();
    // TFOR: the trigger event without an edge. Filling: once the prefill delay ends (the pre-trigger
    // part is then filled), with no comparator armed; Armed: now. Triggered: the gate is already past
    // its event (an edge or the Auto timeout), its record comes as it is. Refused: not running.
    enum class Force { Refused, Forced, Triggered };
    Force force();
    bool timedOut(uint32_t now) const;
    // Its pre-trigger part filled and the comparator armed: an edge (or Auto's timeout, a force) is
    // all it waits for. Not while Filling, nor once triggered (Post, Complete).
    bool armed() const { return phase_==Phase::Armed; }
    static void exti0();
    static void exti15();
    static void timer();
private:
    enum class Phase { Idle,Filling,Armed,Post,Complete };
    void edge(uint32_t line);
    void elapsed();
    void delay(uint64_t ticks);
    bool nextSpan();
    void event(bool external);
    void configureLines();
    static TriggerGate *active_;
    PeripheralResources &resources_;
    PeripheralLease lease_;
    TriggerSink *sink_=nullptr;
    TriggerSettings settings_;
    TriggerDelayPlan delay_;
    volatile Phase phase_=Phase::Idle;
    uint64_t postTicks_=0,preTicks_=0,recordTicks_=0;
    uint32_t clock_=0,line_=0,imr_=0;
    bool linesOwed_=false; // configureLines() ran and releaseLines() has not yet.
    bool prepared_=false;  // prepare() succeeded and neither begin() nor stop() followed.
    volatile uint32_t deadline_=0;
    volatile bool deadlineValid_=false,forcePending_=false;
};
} // namespace openosc::scope
