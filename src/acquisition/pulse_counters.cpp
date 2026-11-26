#include "openosc/acquisition/pulse_counters.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/critical_section.hpp"
#include "openosc/platform/interrupts.hpp"

namespace openosc::scope {
PulseCounters *PulseCounters::active_=nullptr;
namespace {
// External clock mode 2 on ETR (rising edges), no prescaler; the digital filter ETF is CounterWindow's
// level of the window (CounterLadder: 0, 1, 2, 3, 0 per cycle), written only between windows.
uint32_t smcr(uint8_t filter) { return TIM_SMCR_ECE|(uint32_t(filter&15)<<TIM_SMCR_ETF_Pos); }
void pin(GPIO_TypeDef *port,uint32_t pins,uint32_t alternate) {
    GPIO_InitTypeDef gpio{};
    gpio.Pin=pins; gpio.Mode=GPIO_MODE_AF_PP; gpio.Pull=GPIO_NOPULL;
    gpio.Speed=GPIO_SPEED_FREQ_LOW; gpio.Alternate=alternate;
    HAL_GPIO_Init(port,&gpio);
}
void timer(TIM_TypeDef *target,uint32_t maximum) {
    target->CR1=0; target->CR2=0; target->DIER=0; target->SMCR=0; target->CCER=0;
    target->CCMR1=target->CCMR2=0;
    target->PSC=0; target->ARR=maximum; target->EGR=TIM_EGR_UG; target->SR=0;
    target->SMCR=smcr(0);
}
// TIM2 counting CH1's edges on ETR again, from 0, at the window's filter level.
void gateTim2(uint8_t filter) {
    TIM2->CR1=0; TIM2->CCER=0; TIM2->CCMR1=0;
    TIM2->SMCR=smcr(filter); TIM2->EGR=TIM_EGR_UG; TIM2->SR=0;
    TIM2->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
}
// TIM2 counting f_CK_INT from 0 and capturing each rising edge of TI1 (the TIM2_CH1 pin, PA15)
// through a filter of N = 8 samples at f_CK_INT (RM0455 TIMx_CCMR1 IC1F = 0011, TIMx_TISEL TI1SEL = 0).
void captureTim2() {
    TIM2->CR1=0; TIM2->SMCR=0; TIM2->CCER=0;
    TIM2->CCMR1=TIM_CCMR1_CC1S_0|(uint32_t(ReciprocalPolicy::CaptureFilter)<<TIM_CCMR1_IC1F_Pos);
    TIM2->TISEL=0;
    TIM2->CCER=TIM_CCER_CC1E; // CC1P = CC1NP = 0: rising edges.
    TIM2->EGR=TIM_EGR_UG; TIM2->SR=0;
    TIM2->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
}
}
bool PulseCounters::start() {
    if (lease_.valid()) return true;
    const uint32_t clock=TimerClock::apb1();
    if (clock<1000000 || clock%1000000) return false;
    kernelHz_=clock;
    if (!resources_.acquire(PeripheralResources::ScopeCounters,lease_)) return false;
    __HAL_RCC_TIM2_CLK_ENABLE(); __HAL_RCC_TIM4_CLK_ENABLE();
    __HAL_RCC_TIM5_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE();
    pin(GPIOA,GPIO_PIN_15,GPIO_AF1_TIM2); pin(GPIOE,GPIO_PIN_0,GPIO_AF2_TIM4);
    timer(TIM2,UINT32_MAX); timer(TIM4,65535);
    TIM5->CR1=0; TIM5->CR2=0; TIM5->DIER=0; TIM5->SMCR=0; TIM5->CCER=0;
    TIM5->CCMR1=TIM5->CCMR2=0;
    TIM5->PSC=clock/1000000-1; TIM5->ARR=UINT32_MAX;
    TIM5->EGR=TIM_EGR_UG; TIM5->SR=0;
    TIM5->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
    policy_.configure(clock); policy_.enable(timed_);
    high_=0; active_=this;
    Interrupts::attach(TIM4_IRQn,interrupt,5);
    TIM4->DIER=TIM_DIER_UIE;
    TIM2->CR1=TIM4->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
    invalidate(Board::milliseconds());
    return true;
}
void PulseCounters::interrupt() {
    if (active_ && (TIM4->SR&TIM_SR_UIF)) {
        ++active_->high_;
        TIM4->SR=~TIM_SR_UIF; // W0C: preserve unrelated pending flags.
    }
}
// One rising edge of CH1 while timing: two register reads, the log's integer stores (EdgeLog::record).
void PulseCounters::capture() {
    PulseCounters *const self=active_;
    const uint32_t status=TIM2->SR;
    if (!self) { TIM2->DIER=0; return; }
    if (!(status&TIM_SR_CC1IF)) return;
    const uint32_t stamp=TIM2->CCR1; // Clears CC1IF.
    if (status&TIM_SR_CC1OF) { TIM2->SR=~TIM_SR_CC1OF; self->log_.overrun(); TIM2->DIER=0; return; }
    if (!self->log_.record(stamp)) TIM2->DIER=0; // The fast guard: the foreground hands back.
}
uint32_t PulseCounters::read(uint32_t (&counts)[2]) const {
    CriticalSection guard;
    const uint32_t began=TIM5->CNT;
    // While timing, TIM2 counts f_CK_INT: CH1's count is the capture's edge total.
    counts[0]=policy_.timing() ? log_.total() : uint32_t(TIM2->CNT);
    const uint32_t high=high_; const uint16_t low=TIM4->CNT;
    const bool pending=TIM4->SR&TIM_SR_UIF;
    const uint16_t after=pending ? TIM4->CNT : low;
    counts[1]=CounterWindow::extend16(high,low,pending,after);
    // TIM5 provides an independent free-running microsecond reference.
    // Take it with the counts, before an acquisition IRQ can delay the caller.
    return began+uint32_t(TIM5->CNT-began)/2;
}
void PulseCounters::filter(uint8_t level) const {
    TIM4->SMCR=smcr(level);
    if (!policy_.timing()) TIM2->SMCR=smcr(level); // Timing, TIM2 counts its internal clock.
}
void PulseCounters::publish(unsigned channels) {
    ++snapshot_.sequence;
    for (unsigned c=0;c<2;++c) if (channels&(1U<<c)) ++snapshot_.updates[c];
}
void PulseCounters::invalidate(uint32_t now) {
    if (!lease_.valid()) return;
    filter(0); // A cycle starts unfiltered.
    uint32_t counts[2]; const uint32_t cycles=read(counts);
    window_.begin(counts,cycles,now,1000000,kernelHz_);
    polledAt_=now;
    policy_.forget();
    for (auto &channel : snapshot_.channels) channel={};
    publish(3);
    if (!policy_.timing()) return;
    // The input may have changed (source, level, scale): the timed window starts over.
    uint32_t at;
    { CriticalSection guard; log_.restart(); at=TIM2->CNT; }
    policy_.restart(at,now);
}
void PulseCounters::service(uint32_t now) {
    if (!lease_.valid() || uint32_t(now-polledAt_)<10) return;
    polledAt_=now;
    const uint32_t sequence=window_.snapshot().sequence;
    uint32_t counts[2]; uint32_t cycles=read(counts);
    if (window_.observe(counts,cycles,now)) {
        // The next window, at its filter level, from counts taken after the change.
        filter(window_.filter());
        cycles=read(counts);
        window_.restart(counts,cycles,now);
    }
    if (window_.snapshot().sequence!=sequence) cycle(now);
    if (policy_.timing()) serviceTimed(now);
}
void PulseCounters::cycle(uint32_t now) {
    const CounterSnapshot &gate=window_.snapshot();
    snapshot_.channels[1]=gate.channels[1];
    if (policy_.timing()) { publish(2); return; } // CH1's gate windows count the capture's edges.
    snapshot_.channels[0]=policy_.gate(gate.channels[0],now);
    publish(3);
    if (policy_.enters(gate.channels[0],now)) enter(gate.channels[0],now);
}
void PulseCounters::enter(const CounterReading &gate,uint32_t now) {
    TIM2->DIER=0;
    log_.arm(policy_.fastTicks()); // No interrupt yet: nothing else writes the log.
    captureTim2();
    policy_.enter(gate,TIM2->CNT,now);
    uint32_t counts[2]; read(counts);
    window_.rebase(0,counts[0]);
    Interrupts::attach(TIM2_IRQn,capture,CapturePriority);
    TIM2->DIER=TIM_DIER_CC1IE;
}
void PulseCounters::serviceTimed(uint32_t now) {
    EdgeWindow window;
    if (policy_.stalled(now)) { // The tick counter may have wrapped since the last poll.
        uint32_t at;
        { CriticalSection guard; log_.restart(); at=TIM2->CNT; }
        policy_.restart(at,now);
        return;
    }
    {
        CriticalSection guard;
        const uint32_t at=TIM2->CNT; // Every logged stamp is earlier; a later edge is pending.
        window=log_.take(at,TIM2->SR&TIM_SR_CC1IF,policy_.windowTicks());
    }
    const auto event=policy_.step(window,now);
    if (event==ReciprocalPolicy::Event::Reading || event==ReciprocalPolicy::Event::Fast) {
        snapshot_.channels[0]=policy_.reading();
        publish(1);
    }
    if (event!=ReciprocalPolicy::Event::None && event!=ReciprocalPolicy::Event::Reading) leave(event,now);
}
void PulseCounters::leave(ReciprocalPolicy::Event why,uint32_t now) {
    TIM2->DIER=0;
    Interrupts::detach(TIM2_IRQn);
    gateTim2(window_.filter());
    policy_.leave(why,now);
    uint32_t counts[2]; read(counts);
    window_.rebase(0,counts[0]);
    if (why==ReciprocalPolicy::Event::Fast) return; // Its last reading stands.
    snapshot_.channels[0]=policy_.stated();
    publish(1);
}
void PulseCounters::timed(bool on) {
    timed_=on;
    policy_.enable(on);
    if (!on && lease_.valid() && policy_.timing()) leave(ReciprocalPolicy::Event::Disabled,Board::milliseconds());
}
void PulseCounters::stop() {
    if (!lease_.valid()) return;
    if (policy_.timing()) { TIM2->DIER=0; Interrupts::detach(TIM2_IRQn); TIM2->SR=0; }
    TIM2->CR1=TIM4->CR1=TIM5->CR1=0; TIM4->DIER=0;
    Interrupts::detach(TIM4_IRQn); TIM4->SR=0; active_=nullptr;
    GPIO_InitTypeDef gpio{}; gpio.Mode=GPIO_MODE_INPUT; gpio.Pull=GPIO_NOPULL;
    gpio.Pin=GPIO_PIN_15; HAL_GPIO_Init(GPIOA,&gpio);
    gpio.Pin=GPIO_PIN_0; HAL_GPIO_Init(GPIOE,&gpio);
    lease_.release();
    policy_.reset();
    const uint32_t counts[2]={}; window_.begin(counts,0,0,0);
    for (auto &channel : snapshot_.channels) channel={};
    publish(3);
}
} // namespace openosc::scope
