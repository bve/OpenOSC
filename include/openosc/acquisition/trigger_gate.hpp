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
    ~TriggerGate() { stop(); }
    // prepare() does everything that can fail (checks, delay plan, the TIM14/EXTI lease); begin()
    // then arms the prefill delay and cannot fail. start() is both, for callers with no DMA to order.
    // `settleMs` lengthens the prefill: no trigger, Auto timeout or force before it has passed.
    // `slack` points (TriggerRecord::slack) lengthen both the prefill and the post-trigger delay: the
    // ring then holds them on each side of the event for the edge the record is cut at.
    bool prepare(const TriggerSettings &settings,Rational rate,uint32_t samples,uint32_t settleMs=0,uint32_t slack=0);
    void begin(TriggerSink &sink);
    bool start(const TriggerSettings &settings,Rational rate,uint32_t samples,TriggerSink &sink) {
        if (!prepare(settings,rate,samples)) return false;
        begin(sink); return true;
    }
    void stop();
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
    static void edge(int irq);
    void elapsed();
    void delay(uint64_t ticks);
    bool nextSpan();
    void event(bool external);
    static TriggerGate *active_;
    PeripheralResources &resources_;
    PeripheralLease lease_;
    TriggerSink *sink_=nullptr;
    TriggerSettings settings_;
    TriggerDelayPlan delay_;
    volatile Phase phase_=Phase::Idle;
    uint64_t postTicks_=0,preTicks_=0,recordTicks_=0;
    uint32_t clock_=0,line_=0;
    int irq_=0; // line_'s IRQn_Type, enabled only while Armed.
    volatile uint32_t deadline_=0;
    volatile bool deadlineValid_=false,forcePending_=false;
};
} // namespace openosc::scope
