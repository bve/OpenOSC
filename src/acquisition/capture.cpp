#include "openosc/acquisition/capture.hpp"
#include "openosc/acquisition/capture_faults.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/dma_stream.hpp"
#include "openosc/platform/interrupts.hpp"
#include "openosc/platform/critical_section.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "capture_lanes.hpp"
#include "openosc/acquisition/history_schedule.hpp"
#include "openosc/acquisition/sample_plan.hpp"
#include "openosc/acquisition/trigger_record.hpp"
#include <algorithm>

extern "C" { __attribute__((used)) OpenoscCaptureFaults openosc_capture_faults{}; }
#ifdef OPENOSC_DIAG_MAILBOX
#include "openosc/acquisition/first_fault.hpp"
extern "C" { __attribute__((used)) OpenoscFirstFault openosc_first_fault{}; }
#endif
namespace openosc::scope {
#ifdef OPENOSC_DIAG_MAILBOX
// The first capture fault of any kind with the capture's registers as they stand where it is detected,
// before the caller's cleanup; after it only the next LaterFaults values and times. Out of line with its
// own critical section, so its order (every field, DMB, then `valid`) can be read in the image.
__attribute__((noinline)) void recordFirstFault(uint32_t value, uint32_t count) {
    CriticalSection guard;
    auto &r = openosc_first_fault;
    if (r.valid == FirstFaultValid) {
        const uint32_t n = r.later;
        if (n < LaterFaults) { r.laterFault[n] = value; r.laterMillis[n] = Board::milliseconds(); __DMB(); r.later = n + 1; }
        return;
    }
    r.version = FirstFaultVersion; r.fault = value; r.count = count; r.millis = Board::milliseconds();
    r.cycles = DWT->CYCCNT; r.later = 0;
    for (unsigned lane = 0; lane < 4; ++lane) {
        const auto *s = capture_dma::streams[lane];
        r.laneCR[lane] = s->CR; r.laneNDTR[lane] = s->NDTR; r.laneFCR[lane] = s->FCR; r.laneM0AR[lane] = s->M0AR;
    }
    r.dmaISR[0] = DMA1->LISR; r.dmaISR[1] = DMA1->HISR; r.dmaISR[2] = DMA2->LISR; r.dmaISR[3] = DMA2->HISR;
    r.tim3CR1 = TIM3->CR1; r.tim3DIER = TIM3->DIER; r.tim3SR = TIM3->SR; r.tim3SMCR = TIM3->SMCR;
    HistoryCopy::snapshot(r);
    __DMB(); // Every field before `valid`.
    r.valid = FirstFaultValid;
}
#endif
void captureFault(uint32_t value) {
    {
        CriticalSection guard;
        openosc_capture_faults.last = value;
        openosc_capture_faults.count = openosc_capture_faults.count + 1;
#ifdef OPENOSC_DIAG_MAILBOX
        recordFirstFault(value, openosc_capture_faults.count);
#endif
    }
    usbtrace::mark(usbtrace::Tag::CaptureFault, value);
}
CaptureDma *CaptureDma::active_ = nullptr;
using namespace capture_dma;
bool CaptureDma::quiesce(bool stopAdc) {
    gate_.stop();
    // The timer makes no new CC event, every stream is switched off and proven stopped with its FIFO
    // flushed, and only then is that lane's request enable cleared (capture_lanes.hpp, RM0455 15.3.19).
    stopEvents();
    // Every stream is stopped even when another would not: stop as much as possible, then report it.
    bool stopped = true;
    // A split ring's copies stop first: no MDMA block runs past the lanes' end.
    if (!history_.stop()) {
        captureFault(1U << 20);
        stopped = false;
    } else {
        Interrupts::detach(MDMA_IRQn);
    }
    for (unsigned i = 0; i < 4; ++i) {
        // EN=0 raises TCIF: masked, or the ISR takes it for a finished segment mid-stop.
        const bool enabled = NVIC_GetEnableIRQ(irqs[i]);
        NVIC_DisableIRQ(irqs[i]);
        if (!DmaStream::disable(streams[i])) {
            if (enabled) NVIC_EnableIRQ(irqs[i]);
            captureFault(1U << 16 | i << 8 | (streams[i]->FCR & 0x3fU));
            stopped = false;
            continue;
        }
        releaseRequest(i);
        Interrupts::detach(irqs[i]);
        controller(i)->LIFCR = flags(i);
    }
    // The ADC clock stays up until every stream and channel is proven stopped, also on a failed stop
    // (the stopped timer then keeps the request enable of each stream not proven stopped): its retry
    // stops them in the same order. A requested ADC stop waits for that retry.
    if (stopped) {
        // Every stream is stopped: the trigger's EXTI line may be cleared now without stalling a
        // lane's GPIOD read on the shared SRD bus (GitHub #33, TriggerGate::releaseLines).
        gate_.releaseLines();
        if (stopAdc || adcStopPending_) clock_.stop();
        adcStopPending_ = false;
    } else if (stopAdc) {
        adcStopPending_ = true;
    }
    __DSB();
    stopFailed_ = !stopped;
    if (!stopped) { status_ = DataStatus::HardwareError; stopRetryAt_ = Board::milliseconds() + StopRetryMs; }
    return stopped;
}
void CaptureDma::abandon(DataStatus status) {
    if (quiesce(false)) { mailbox_.cancel(); status_ = status; }
}
bool CaptureDma::start(FrameMetadata metadata, const FrontendSettings &settings,const CaptureBuffer &buffer,bool rolling,
                       int16_t triggerLevel,const TriggerSettings *trigger) {
    const uint32_t began=Board::milliseconds();
    const bool gated=trigger && !rolling;
    // A finite record fills the buffer exactly once; triggered and Roll records ring through it.
    if (mailbox_.state() != FrameMailbox::State::Idle || !buffer.valid() || metadata.layout != buffer.layout() ||
        !metadata.valid() || metadata.samples != metadata.ring || metadata.samples % (4 * buffer.pointsPerTransfer()) ||
        metadata.origin ||
        metadata.samples > buffer.capacityPoints() || (!gated && metadata.samples != buffer.ringPoints()) ||
        (buffer.split() && !gated) || // A split ring is a triggered ring: only its freeze settles the copies.
        (!gated && buffer.segments() != 1) || (rolling && (!buffer.words() || !metadata.rolling)) ||
        (trigger && !trigger->valid()) ||
        // Only an interleaved buffer is captured with the front end (and so the ADC clocks) interleaved.
        (buffer.format == SampleFormat::InterleavedWords) != settings.interleave) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    // The timers run at the transfer rate; the record's points come pointsPerTransfer() per transfer.
    const bool interleaved = settings.interleave;
    Rational pointRate;
    CaptureTiming timing;
    if (TimerClock::apb1() != TimerClock::apb2() ||
        !CaptureTiming::points(TimerClock::apb1(), metadata.samplesPerSecond, buffer.pointsPerTransfer(), timing, pointRate)) {
        status_ = DataStatus::Unsupported;
        return false;
    }
    // A refusal before the hardware gives back only what this start claimed: a lease held from before
    // stays, as its stop may not be proven yet (stopFailed_).
    const bool freshDma=!lease_.valid();
    if (freshDma && !resources_.acquire(PeripheralResources::ScopeDma, lease_)) {
        status_ = DataStatus::Busy;
        return false;
    }
    // A split ring also takes the pacing streams and their sink (and the MDMA channels and generators they
    // stand for); any other capture gives them back once the start's quiesce proved them stopped.
    const bool freshHistory=buffer.split() && !historyLease_.valid();
    if (freshHistory && !resources_.acquire(PeripheralResources::ScopeHistory, historyLease_)) {
        if (freshDma) lease_.release();
        status_ = DataStatus::Busy;
        return false;
    }
    if (!frontend_.scope(settings,triggerLevel)) {
        if (freshHistory) historyLease_.release();
        if (freshDma) lease_.release();
        status_ = DataStatus::Uncalibrated;
        return false;
    }
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_AHBSRAM1_CLK_ENABLE();
    __HAL_RCC_AHBSRAM2_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    adcStopPending_ = false; // A new capture wants the ADC clock: no stop and restart in between.
    if (!quiesce(false))
        return false;
    if (!buffer.split()) historyLease_.release();
    if (!clock_.running()) {
        GPIO_InitTypeDef gpio{};
        gpio.Pin=GPIO_PIN_All; gpio.Mode=GPIO_MODE_INPUT; gpio.Pull=GPIO_NOPULL;
        HAL_GPIO_Init(GPIOD,&gpio);
        clock_.start(timing.prescaler, timing.period, TimerClock::apb2(), interleaved); // At the plan: never faster.
    }
    TIM3->CR1 = 0;
    TIM3->CR2 = 0;
    TIM3->SMCR = 0; // Do not acquire the stale first conversion pipeline during ADC clock warm-up.
    // The ADCs convert once per sample; a rate change updates TIM1 (TRGO) only now TIM3 ignores it.
    clock_.configure(timing.prescaler, timing.period, TimerClock::apb2(), interleaved); // TIM1 is on APB2.
    TIM3->PSC = timing.prescaler;
    TIM3->ARR = timing.period * 4 - 1;
    TIM3->CCR1 = timing.laneCompare(0);
    TIM3->CCR2 = timing.laneCompare(1);
    TIM3->CCR3 = timing.laneCompare(2);
    TIM3->CCR4 = timing.laneCompare(3);
    TIM3->CCMR1 = 0;
    TIM3->CCMR2 = 0;
    TIM3->CCER = 0;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->CNT = 0;
    TIM3->SR = 0;
    metadata.samplesPerSecond = pointRate;
    if (rolling) metadata.timestampUs+=uint64_t(Board::milliseconds()-began)*1000+
        uint64_t(500000.0/timing.sampleRate.value());
    rolling_=rolling; gated_=gated; frozen_=false; forced_=false;
    requestedSamples_=metadata.samples;
    if (gated_) { metadata.ring=buffer.ringPoints(); trigger_=*trigger; } // Pre/post history rings through the buffer.
    metadata_ = metadata;
    buffer_ = buffer;
    for (auto &cycle:cycles_) cycle=0;
    complete_ = 0;
    active_ = this;
    if (!mailbox_.begin()) {
        if (freshHistory) historyLease_.release();
        if (freshDma) lease_.release();
        active_ = nullptr; status_ = DataStatus::Busy; return false;
    }
    // Everything that can still fail happens before the first DMA write: a refused start leaves
    // the buffer exactly as it was (resumeScope relies on this to keep a 512 KiB HOLD record).
    // After an analog change the gate fills on until the front end has settled: every point of the
    // record, its pre-trigger part included, is then taken at the settings its metadata describes.
    // Slack on both sides of the event: the edge the record is cut at (TriggerRecord::select) need not
    // be the crossing the comparator fired on.
    slack_=gated_ ? TriggerRecord::slack(requestedSamples_,buffer.capacityPoints(),
                                         SamplePlan::headroom(buffer,pointRate.numerator/pointRate.denominator)) : 0;
    if (gated_ && !gate_.prepare(trigger_,metadata.samplesPerSecond,requestedSamples_,frontend_.unsettledMs(Board::milliseconds()),slack_)) {
        captureFault(1U << 18);
        if (freshHistory) historyLease_.release();
        if (freshDma) lease_.release();
        mailbox_.cancel(); active_=nullptr; status_=DataStatus::HardwareError; return false;
    }
    const bool bytes=buffer.format==SampleFormat::Bytes, doubleBuffered=buffer.segments()>1;
    for (unsigned i = 0; i < 4; ++i) {
        auto *stream = streams[i];
        mux[i]->CCR = 23 + i;
        // One channel reads its own ADC byte: GPIOD bits 0-7 are CH1, 8-15 CH2.
        stream->PAR = uint32_t(reinterpret_cast<uintptr_t>(&GPIOD->IDR)) + (bytes && buffer.byteChannel == Channel::Ch2 ? 1U : 0U);
        stream->M0AR = uint32_t(reinterpret_cast<uintptr_t>(buffer.lanes[i]));
        // A lane longer than NDTR alternates between two segment registers (DBM).
        if (doubleBuffered) stream->M1AR = uint32_t(reinterpret_cast<uintptr_t>(buffer.lanes[i])) + buffer.segmentBytes();
        stream->NDTR = buffer.segmentTransfers;
        // Finite/triggered records freeze and drain before readers see RAM.
        // The FIFO absorbs short memory-port stalls: flushing at half full in INCR4 bursts leaves
        // room for 4 more word requests (~114 ns at 140 MS/s); a longer stall of this controller's
        // memory port raises FE (AcquisitionArena::fullRate). Bytes are packed in pairs, so the
        // same 8-byte bursts carry eight points and twice the margin.
        // Roll keeps direct mode: its live NDTR prefix must already be in RAM.
        stream->FCR = DMA_SxFCR_FEIE | (!rolling ? DMA_SxFCR_DMDIS|DMA_SxFCR_FTH_0 : 0U);
        // Below the NOR streams: SPI needs little bandwidth but starves behind these at 140 MS/s.
        stream->CR = (2U << 16) | DMA_SxCR_MSIZE_0 | (bytes ? 0U : DMA_SxCR_PSIZE_0) | DMA_SxCR_MINC | DMA_SxCR_TCIE |
                     DMA_SxCR_TEIE | DMA_SxCR_DMEIE | ((rolling || gated_) ? DMA_SxCR_CIRC : 0U) |
                     (doubleBuffered ? DMA_SxCR_DBM : 0U) | (!rolling ? DMA_SxCR_MBURST_0 : 0U);
    }
    if (buffer.split()) {
        for (auto &low : leadLow_) low=UINT32_MAX;
        leadSamples_=leadFaults_=0;
        history_.arm(buffer); // Paced by lane 0 from its first request on.
        Interrupts::attach(MDMA_IRQn, mdma);
    }
    Interrupts::attach(DMA1_Stream0_IRQn, dma1Stream0);
    Interrupts::attach(DMA1_Stream1_IRQn, dma1Stream1);
    Interrupts::attach(DMA2_Stream0_IRQn, dma2Stream0);
    Interrupts::attach(DMA2_Stream1_IRQn, dma2Stream1);
    status_ = DataStatus::Ok;
    const uint32_t duration = uint32_t(1000.0 * metadata.samples / pointRate.value());
    deadline_ = Board::milliseconds() + duration + 1000;
    uint64_t segmentTicks=0; // TC marks the end of each segment of every lane.
    TriggerDelayPlan::sampleTicks(buffer.segmentTransfers*4,timing.sampleRate,SystemCoreClock,segmentTicks);
    cycleGuard_.begin(segmentTicks,DWT->CYCCNT);
    __DMB();
    // One step for the DMA IRQs: an error at enable must find the capture started with its armed
    // gate, so its quiesce stops both. Between EN and begin() it would quiesce the capture, and the
    // lines below would then restart TIM3 requests and arm the gate over the stopped capture.
    CriticalSection guard;
    for (auto *stream : streams)
        stream->CR |= DMA_SxCR_EN;
    TIM3->DIER = TIM_DIER_CC1DE | TIM_DIER_CC2DE | TIM_DIER_CC3DE | TIM_DIER_CC4DE;
    TIM3->SMCR = TIM_SMCR_SMS_1 | TIM_SMCR_SMS_2;
    if (gated_) gate_.begin(*this);
    return true;
}
void CaptureDma::interrupt(unsigned bank) {
    // Clearing TC and updating the epoch are one handoff. A trigger IRQ between
    // them would miss a real wrap even if the 64-bit increment itself were atomic.
    CriticalSection guard;
    const uint32_t status = controller(bank)->LISR;
    const unsigned shift = bank < 2 ? 6 : 0;
    controller(bank)->LIFCR = flags(bank);
    if (status & (0x0dU << shift)) {
        captureFault(((status >> shift) & 0x3fU) | bank << 8);
        // Counted, never masked: the capture ends with HardwareError and is retried after
        // CaptureRetryMs. The external ADC clock keeps running for that retry; whether its
        // restarts cause the USB33RDY dips seen near capture starts is not established.
        abandon(DataStatus::HardwareError);
        return;
    }
    if (!(status & (0x20U << shift)))
        return;
    if (rolling_ || gated_) {
        const bool inTime=cycleGuard_.observe(bank,DWT->CYCCNT);
        if (!inTime || !advanceSegment(bank,++cycles_[bank])) {
            captureFault(1U << 21 | bank << 8 | (inTime ? 2U : 1U) | uint32_t(cycles_[bank] & 0xff) << 12);
            abandon(DataStatus::Overflow); return;
        }
        if (rolling_ && !bank) ++completed_;
        return;
    }
    complete_ |= 1U << bank;
    if (complete_ != 15)
        return;
    ++completed_;
    if (!quiesce(false))
        return;
    const uint16_t *banks[4] = {buffer_.lanes[0], buffer_.lanes[1], buffer_.lanes[2], buffer_.lanes[3]};
    __DMB();
    mailbox_.publish(FrameView(metadata_, banks, buffer_.wordsPerLane()));
}
bool CaptureDma::advanceSegment(unsigned lane,uint64_t completed) {
    const uint32_t segments=buffer_.segments();
    if (segments<=2) return true; // Two segments alternate in M0AR/M1AR as programmed.
    // Segment `completed-1` just ended; the stream now writes segment `completed` from the other
    // register. The idle register gets the segment after that, which is due one segment from now.
    auto *stream=streams[lane];
    const bool secondCurrent=stream->CR & DMA_SxCR_CT;
    if (secondCurrent!=bool(completed&1)) return false; // A completion was missed: the ring is ambiguous.
    const uint32_t address=uint32_t(buffer_.segmentAddress(lane,completed+1));
    if (secondCurrent) stream->M0AR=address; else stream->M1AR=address;
    return true;
}
bool CaptureDma::stop(bool stopClock) {
    if (!lease_.valid()) {
        if (stopClock) clock_.stop();
        return true;
    }
    if (!quiesce(stopClock))
        return false;
    mailbox_.cancel();
    if (mailbox_.state() == FrameMailbox::State::Ready) {
        FrameLease pending;
        if (mailbox_.acquire(pending))
            pending.release();
    }
    active_ = nullptr;
    rolling_=gated_=false; frozen_=false;
    historyLease_.release();
    lease_.release();
    return true;
}
void CaptureDma::service(uint32_t now) {
    // A stop that failed: the capture still owns its memory; give it up once a stop succeeds.
    if (stopFailed_) {
        if (int32_t(now-stopRetryAt_)>=0 && quiesce(false) && mailbox_.state()==FrameMailbox::State::Capturing) mailbox_.cancel();
        return;
    }
    if (frozen_) { publishTriggered(); return; }
#ifdef OPENOSC_USB_TRACE
    if (history_.armed() && mailbox_.state()==FrameMailbox::State::Capturing) traceLead();
#endif
    const bool timeout=gated_ ? gate_.timedOut(now) : !rolling_ && int32_t(now-deadline_)>=0;
    if (mailbox_.state()==FrameMailbox::State::Capturing && timeout) abandon(DataStatus::Timeout);
}
void CaptureDma::traceLead() {
    // Each lane's copy must read every ring word before the lane writes it again. Allowed per lane:
    // more than 0 and at most Lead + 1 blocks and 2 words ahead; a copy that fell behind its lane
    // reads as nearly a whole ring. Every out-of-range sample and each lane's new smallest lead are
    // traced. Polled samples only: no claim about the times between them.
    const uint32_t most=(HistorySchedule::Lead+1)*buffer_.copyTransfers+2;
    for (unsigned lane=0;lane<4;++lane) {
        uint32_t lead;
        { CriticalSection guard; lead=history_.lead(lane,streams[lane]->NDTR); }
        const bool outside=!lead || lead>most;
        if (outside) ++leadFaults_;
        if ((outside && leadFaults_<=16) || lead<leadLow_[lane]) {
            if (!outside) leadLow_[lane]=lead;
            usbtrace::mark(usbtrace::Tag::CaptureLead, std::min<uint32_t>(lead,0xffff) | lane << 16 | (outside ? 1U << 18 : 0U) |
                           std::min<uint32_t>(leadSamples_,0x1fff) << 19);
        }
    }
    ++leadSamples_;
}
void CaptureDma::historyFault() {
    uint32_t error = 0;
    const uint32_t lanes = HistoryCopy::failed(error);
    if (lanes) captureFault(1U << 23 | lanes | (error & 0xfffU) << 4); // Counted while TEIF still stands.
    HistoryCopy::clearInterrupts();
    if (!lanes) return;
    abandon(DataStatus::HardwareError);
}
void CaptureDma::mdma() {
    if (active_) active_->historyFault();
    else HistoryCopy::clearInterrupts();
}
void CaptureDma::dma1Stream0() {
    if (active_)
        active_->interrupt(2);
}
void CaptureDma::dma1Stream1() {
    if (active_)
        active_->interrupt(0);
}
void CaptureDma::dma2Stream0() {
    if (active_)
        active_->interrupt(3);
}
void CaptureDma::dma2Stream1() {
    if (active_)
        active_->interrupt(1);
}
} // namespace openosc::scope
