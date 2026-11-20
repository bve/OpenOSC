#include "openosc/acquisition/trigger_gate.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/interrupts.hpp"
#include "openosc/platform/critical_section.hpp"
#include <algorithm>

namespace openosc::scope {
TriggerGate *TriggerGate::active_=nullptr;
namespace { constexpr uint32_t Lines=(1U<<0)|(1U<<15); }
// GitHub #33: EXTI, SYSCFG and RCC sit on the SmartRun domain (SRD APB4/AHB4) with GPIOD, the ADC
// bus every capture lane's DMA reads. A CPU access to them while the streams run stalls those reads:
// the lanes sample late, then repeat (ragged patches round the trigger point). So the gate touches
// SRD only while no capture DMA runs (prepare(), releaseLines()) and once more while it does: the
// single write that unmasks the comparator line at arming. The edge, the Auto timeout, a force and
// stop() use the core's NVIC and TIM14 (CD APB1) only. A masked line latches no pending request
// (ZT703S, comparator at 2 MHz: masked PR1 0, unmasked 0x8000, masked and cleared 0 again:
// build/reviews/issue33-20261120/hardware/exti-mask-semantics.json; RM0455 Rev 11 says the same,
// build/research/st-usb/rm0455-rev11.txt:39902), so the line masked during the prefill gives no
// stale edge at arming; its pending bit, left set by the edge, is cleared with the rest once the
// capture is stopped.
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
    line_=settings_.channel==Channel::Ch1 ? 1U<<15 : 1U;
    configureLines();
    prepared_=true;
    return true;
}
// Before the capture's DMA starts (CaptureDma::start: nothing fails after prepare()): every SRD
// register the gate needs, the comparator line masked. The selected edge is already set; a masked
// line records no pending request (see above), so arming finds none from the prefill.
void TriggerGate::configureLines() {
    __HAL_RCC_TIM14_CLK_ENABLE(); __HAL_RCC_SYSCFG_CLK_ENABLE();
    // Comparator pads are also the external-clock inputs for TIM2/TIM4.
    // EXTI routing does not change their GPIO alternate-function configuration.
    SYSCFG->EXTICR[0]=(SYSCFG->EXTICR[0]&~15U)|4U; // PE0, CH2
    SYSCFG->EXTICR[3]&=~(15U<<12); // PA15, CH1
    EXTI->IMR1&=~Lines;
    // Factory mapping is inverted at the comparator pad: analog rising -> GPIO falling.
    if (settings_.edge==Edge::Rising) { EXTI->RTSR1&=~Lines; EXTI->FTSR1=(EXTI->FTSR1&~Lines)|line_; }
    else { EXTI->FTSR1&=~Lines; EXTI->RTSR1=(EXTI->RTSR1&~Lines)|line_; }
    EXTI->PR1=Lines; // W1C: no request left from an earlier capture.
    // The mask arming writes back with the line set: a write, no read, then. This cached full write
    // assumes the gate is the only EXTI user (true: no other code in src touches EXTI).
    imr_=EXTI->IMR1;
    linesOwed_=true;
}
// The lease is held until here, not just until stop(): while the line is owed, no other gate may take
// and configure EXTI, which this cleanup would then clear.
void TriggerGate::releaseLines() {
    if (phase_!=Phase::Idle || prepared_) return; // Running, or prepared for a begin(): not now.
    if (linesOwed_) { EXTI->IMR1&=~Lines; EXTI->RTSR1&=~Lines; EXTI->FTSR1&=~Lines; EXTI->PR1=Lines; }
    linesOwed_=false;
    lease_.release();
}
void TriggerGate::begin(TriggerSink &sink) {
    CriticalSection guard;
    // Checked with interrupts masked: a capture DMA error IRQ that quiesced the capture (and so
    // released this lease) after the check would otherwise arm a gate that owns nothing, whose
    // Complete phase no stop() could clear, refusing every later prepare().
    if (!lease_.valid() || !prepared_ || phase_!=Phase::Idle) return;
    prepared_=false;
    TIM14->CR1=0; TIM14->DIER=0; TIM14->SR=0;
    sink_=&sink; forcePending_=false;
    active_=this; phase_=Phase::Filling;
    Interrupts::attach(TIM8_TRG_COM_TIM14_IRQn,timer,3); // The comparator's vector only at arming.
    delay(preTicks_);
}
void TriggerGate::stop() {
    CriticalSection guard;
    // Only a gate that neither holds its lease nor runs has nothing to stop. A running gate is
    // always stopped, so no state can keep the gate out of Idle.
    if (!lease_.valid() && phase_==Phase::Idle) return;
    prepared_=false; // A stopped gate is not begun again without a new prepare().
    TIM14->CR1=0; TIM14->DIER=0; TIM14->SR=0;
    // NVIC only: the EXTI line is cleared by releaseLines() once the capture's DMA has stopped.
    Interrupts::detach(EXTI0_IRQn); Interrupts::detach(EXTI15_10_IRQn);
    Interrupts::detach(TIM8_TRG_COM_TIM14_IRQn);
    active_=nullptr; sink_=nullptr; phase_=Phase::Idle; deadlineValid_=false; forcePending_=false;
    // The lease stays: the EXTI line is still owed (releaseLines(), once no capture DMA runs).
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
        // The vector first (its NVIC pending cleared, then enabled), then the one SRD write: the
        // line unmasked. Masked through the prefill, it holds no request; from here every edge is
        // taken, after this ISR returns (the same priority).
        if (line_==(1U<<15)) Interrupts::attach(EXTI15_10_IRQn,exti15,3); else Interrupts::attach(EXTI0_IRQn,exti0,3);
        EXTI->IMR1=imr_|line_;
        // Auto timeout: one record (an edge a period away is still found), 20 ms to 1 s.
        if (settings_.mode==TriggerMode::Auto) delay(std::clamp<uint64_t>(recordTicks_,clock_/50,clock_));
    } else if (phase_==Phase::Armed) event(false);
    else if (phase_==Phase::Post) { phase_=Phase::Complete; if (sink_) sink_->freezeRecord(); }
}
void TriggerGate::event(bool external) {
    if (phase_!=Phase::Armed || !sink_) return;
    // No more edges: the vector off at the NVIC (no SRD access). The line and its pending bit stay
    // until releaseLines(); a disabled vector never runs for them.
    Interrupts::detach(line_==(1U<<15) ? EXTI15_10_IRQn : EXTI0_IRQn);
    TIM14->CR1=0; TIM14->DIER=0; TIM14->SR=0;
    phase_=Phase::Post; sink_->triggerEvent(external); delay(postTicks_);
}
// The vector runs only while armed and only for its own line: EXTI0 serves line 0 alone, and of
// EXTI15_10's lines 10-15 only the comparator's line 15 is ever unmasked (no other EXTI user). So
// the pending register is not read here (an SRD access while the capture runs).
void TriggerGate::edge(uint32_t line) {
    if (line==line_) event(true);
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
void TriggerGate::exti0() { if (active_) active_->edge(1); }
void TriggerGate::exti15() { if (active_) active_->edge(1U<<15); }
void TriggerGate::timer() { if (active_) active_->elapsed(); }
} // namespace openosc::scope
