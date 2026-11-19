#include "openosc/acquisition/trigger_gate.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/interrupts.hpp"
#include "openosc/platform/critical_section.hpp"
#include <algorithm>

namespace openosc::scope {
TriggerGate *TriggerGate::active_=nullptr;
namespace { constexpr uint32_t Lines=(1U<<0)|(1U<<15); }
bool TriggerGate::prepare(const TriggerSettings &settings,Rational rate,uint32_t samples,uint32_t settleMs,uint32_t slack) {
    if (!settings.valid() || samples<RecordDepth::Minimum || samples>RecordDepth::Maximum || slack>samples ||
        phase_!=Phase::Idle || lease_.valid()) return false;
    clock_=TimerClock::apb1();
    const uint32_t pre=uint64_t(samples-1)*settings.position/1000;
    if (!TriggerDelayPlan::sampleTicks(samples-1,rate,clock_,recordTicks_) || // The Auto timeout: the record, no slack.
        !TriggerDelayPlan::sampleTicks(pre+slack,rate,clock_,preTicks_) ||
        !TriggerDelayPlan::sampleTicks(samples-1-pre+slack,rate,clock_,postTicks_) ||
        !resources_.acquire(PeripheralResources::ScopeTrigger,lease_)) return false;
    preTicks_+=uint64_t(clock_)*settleMs/1000;
    settings_=settings;
    return true;
}
void TriggerGate::begin(TriggerSink &sink) {
    __HAL_RCC_TIM14_CLK_ENABLE(); __HAL_RCC_SYSCFG_CLK_ENABLE();
    CriticalSection guard;
    // Checked with interrupts masked: a capture DMA error IRQ that quiesced the capture (and so
    // released this lease) after the check would otherwise arm a gate that owns nothing, whose
    // Complete phase no stop() could clear, refusing every later prepare().
    if (!lease_.valid() || phase_!=Phase::Idle) return;
    TIM14->CR1=0; TIM14->DIER=0; TIM14->SR=0;
    EXTI->IMR1&=~Lines; EXTI->RTSR1&=~Lines; EXTI->FTSR1&=~Lines; EXTI->PR1=Lines;
    // Comparator pads are also the external-clock inputs for TIM2/TIM4.
    // EXTI routing does not change their GPIO alternate-function configuration.
    SYSCFG->EXTICR[0]=(SYSCFG->EXTICR[0]&~15U)|4U; // PE0, CH2
    SYSCFG->EXTICR[3]&=~(15U<<12); // PA15, CH1
    sink_=&sink; line_=settings_.channel==Channel::Ch1 ? 1U<<15 : 1U; forcePending_=false;
    irq_=line_==1U ? EXTI0_IRQn : EXTI15_10_IRQn;
    // Factory mapping is inverted at the comparator pad: analog rising -> GPIO falling.
    if (settings_.edge==Edge::Rising) EXTI->FTSR1|=line_;
    else EXTI->RTSR1|=line_;
    EXTI->IMR1|=line_; // Unmasked now; the NVIC gates it: arming touches EXTI once (PR1).
    active_=this; phase_=Phase::Filling;
    Interrupts::attach(EXTI0_IRQn,exti0,3); Interrupts::attach(EXTI15_10_IRQn,exti15,3);
    NVIC_DisableIRQ(EXTI0_IRQn); NVIC_DisableIRQ(EXTI15_10_IRQn);
    Interrupts::attach(TIM8_TRG_COM_TIM14_IRQn,timer,3);
    delay(preTicks_);
}
void TriggerGate::stop() {
    CriticalSection guard;
    // Only a gate that neither holds its lease nor runs has nothing to stop. A running gate is
    // always stopped, so no state can keep the gate out of Idle.
    if (!lease_.valid() && phase_==Phase::Idle) return;
    TIM14->CR1=0; TIM14->DIER=0; TIM14->SR=0;
    EXTI->IMR1&=~Lines; EXTI->RTSR1&=~Lines; EXTI->FTSR1&=~Lines; EXTI->PR1=Lines;
    Interrupts::detach(EXTI0_IRQn); Interrupts::detach(EXTI15_10_IRQn);
    Interrupts::detach(TIM8_TRG_COM_TIM14_IRQn);
    active_=nullptr; sink_=nullptr; phase_=Phase::Idle; deadlineValid_=false; forcePending_=false;
    lease_.release();
}
void TriggerGate::delay(uint64_t ticks) {
    delay_.begin(ticks);
    const uint64_t milliseconds=ticks/clock_*1000+(ticks%clock_)*1000/clock_;
    deadline_=Board::milliseconds()+uint32_t(milliseconds+2000); deadlineValid_=true;
    nextSpan();
}
bool TriggerGate::nextSpan() {
    TriggerDelayPlan::Span span;
    if (!delay_.next(span)) return false;
    TIM14->CR1=0; TIM14->DIER=0;
    TIM14->PSC=span.prescaler; TIM14->ARR=span.reload;
    TIM14->EGR=TIM_EGR_UG; TIM14->CNT=0; TIM14->SR=0;
    TIM14->DIER=TIM_DIER_UIE; TIM14->CR1=TIM_CR1_OPM|TIM_CR1_CEN;
    return true;
}
void TriggerGate::elapsed() {
    if (!(TIM14->SR&TIM_SR_UIF)) return;
    TIM14->SR=~TIM_SR_UIF;
    if (nextSpan()) return;
    TIM14->DIER=0; deadlineValid_=false;
    if (phase_==Phase::Filling) {
        phase_=Phase::Armed;
        if (forcePending_) { forcePending_=false; event(false); return; } // Forced while filling.
        // EXTI and the ADC's GPIO share the D3 bus the capture reads at 2 cycles a sample: two accesses
        // (was five); the read-back makes the posted clear land before the NVIC's pending bit is dropped.
        EXTI->PR1=line_;
        (void)EXTI->PR1;
        NVIC_ClearPendingIRQ(IRQn_Type(irq_)); NVIC_EnableIRQ(IRQn_Type(irq_));
        // Auto timeout: one record (an edge a period away is still found), 20 ms to 1 s.
        if (settings_.mode==TriggerMode::Auto) delay(std::clamp<uint64_t>(recordTicks_,clock_/50,clock_));
    } else if (phase_==Phase::Armed) event(false);
    else if (phase_==Phase::Post) { phase_=Phase::Complete; if (sink_) sink_->freezeRecord(); }
}
void TriggerGate::event(bool external) {
    if (phase_!=Phase::Armed || !sink_) return;
    NVIC_DisableIRQ(IRQn_Type(irq_)); // No EXTI access mid-record; PR1 is cleared at the next arming.
    TIM14->CR1=0; TIM14->DIER=0; TIM14->SR=0;
    phase_=Phase::Post; sink_->triggerEvent(external); delay(postTicks_);
}
// PR1 stays set (no D3 write mid-record): each vector turns itself off; only the armed line triggers.
void TriggerGate::edge(int irq) {
    NVIC_DisableIRQ(IRQn_Type(irq));
    if (active_ && irq==active_->irq_) active_->event(true);
}
TriggerGate::Force TriggerGate::force() {
    CriticalSection guard; // Against the edge and TIM14 interrupts that move the phase.
    switch (phase_) {
    case Phase::Filling: forcePending_=true; return Force::Forced;
    case Phase::Armed: event(false); return Force::Forced;
    case Phase::Post: case Phase::Complete: return Force::Triggered;
    default: return Force::Refused;
    }
}
bool TriggerGate::timedOut(uint32_t now) const { return deadlineValid_ && int32_t(now-deadline_)>=0; }
void TriggerGate::exti0() { edge(EXTI0_IRQn); }
void TriggerGate::exti15() { edge(EXTI15_10_IRQn); }
void TriggerGate::timer() { if (active_) active_->elapsed(); }
} // namespace openosc::scope
