#pragma once
#include "openosc/acquisition/frontend.hpp"
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/acquisition/arena.hpp"
#include "openosc/acquisition/adc_clock.hpp"
#include "openosc/acquisition/trigger_gate.hpp"
#include "openosc/acquisition/circular_progress.hpp"
#include "openosc/acquisition/dma_cycle_guard.hpp"
#include "openosc/acquisition/history_copy.hpp"
#include "openosc/acquisition/progress.hpp"
#include "openosc/platform/resources.hpp"

namespace openosc::scope {
struct CaptureTiming {
    uint16_t prescaler = 0, period = 2;
    Rational sampleRate;
    // At the timer clock of the running profile (its ADC divider, RunMode).
    static bool calculate(uint32_t clockHz, Rational requestedRate, CaptureTiming &output);
    // With an explicit ADC divider (2, 3 or 4): the shortest ADC period in timer ticks.
    static bool calculate(uint32_t clockHz, uint32_t adcDivider, Rational requestedRate, CaptureTiming &output);
    // A record's point rate through DMA transfers of pointsPerTransfer points each (InterleavedWords:
    // two): the timing of the transfer rate, and the point rate it really gives (pointsPerTransfer x
    // its sampleRate, exactly).
    static bool points(uint32_t clockHz, Rational pointRate, uint32_t pointsPerTransfer, CaptureTiming &output,
                       Rational &actualPointRate);
    // TIM3 compare of DMA lane 0..3 (ARR 4*period-1): one read per ADC period, `period` ticks apart
    // (period 2: 1/3/5/7; period 3: 1/4/7/10, ARR 11; period 4: 2/6/10/14, ARR 15; period 20: 10/30/50/70).
    constexpr uint16_t laneCompare(unsigned lane) const { return uint16_t(period * (2 * lane + 1) / 2); }
};
class CaptureDma : private TriggerSink {
public:
    CaptureDma(PeripheralResources &resources, AnalogFrontend &frontend)
        : resources_(resources), frontend_(frontend),gate_(resources) {}
    ~CaptureDma() { stop(); }
    // Captures into `buffer` until stop(); the caller owns that memory for the whole capture.
    // False means nothing was written to `buffer`: every refusal precedes the first DMA transfer.
    bool start(FrameMetadata metadata, const FrontendSettings &settings,const CaptureBuffer &buffer,bool rolling=false,
               int16_t triggerLevel=0,const TriggerSettings *trigger=nullptr);
    uint64_t rollingAvailable() const;
    CaptureProgress progress() const;
    bool acquire(FrameLease &lease) { return mailbox_.acquire(lease); }
    bool stop(bool stopClock=true);
    // TFOR on the running triggered capture (TriggerGate::force): true when its record is on the way,
    // forced (FrameMetadata::forced) or already triggered; false with no gated capture to force.
    bool force();
    // A running gated record whose trigger gate is Armed (TriggerGate::armed): waiting for its edge.
    bool awaitingTrigger() const { return gated_ && !frozen_ && mailbox_.state()==FrameMailbox::State::Capturing && gate_.armed(); }
    void service(uint32_t nowMs);
    FrameMailbox::State state() const { return mailbox_.state(); }
    DataStatus status() const { return status_; }
    const FrameMetadata &metadata() const { return metadata_; }
    uint32_t completed() const { return completed_; }
    // The last published frame had no trigger edge anywhere in the ring it was cut from, for the
    // settings returned: a search of that frame with them finds nothing (TriggerCache::remember).
    bool edgeless(const FrameMetadata &frame, TriggerSettings &settings) const {
        if (!edgeless_ || edgelessSequence_ != frame.sequence || edgelessTimestamp_ != frame.timestampUs) return false;
        settings = trigger_;
        return true;
    }
    static void dma1Stream0();
    static void dma1Stream1();
    static void dma2Stream0();
    static void dma2Stream1();
    static void mdma();

private:
    void interrupt(unsigned bank);
    bool advanceSegment(unsigned lane,uint64_t completed); // Double-buffer rings longer than two segments.
    // Stops the gate, TIM3's requests, the split copies and every stream, then TIM3 (and with stopAdc
    // the ADC clock); true only once each stream and channel is proven disabled. A failure leaves
    // stopFailed_ set and the gated timer and clock running: the capture keeps its memory and its
    // leases, and service() tries again.
    bool quiesce(bool stopAdc);
    // Ends a capture that delivers nothing: the mailbox (and so the memory) is given up only once
    // quiesce() proved every DMA stream stopped; else it stays Capturing.
    void abandon(DataStatus status);
    void laneCounts(uint64_t (&counts)[4],uint32_t *remaining=nullptr) const; // Caller masks interrupts; DMA may still be running.
    uint64_t laneCount(unsigned lane,uint32_t *remaining=nullptr) const; // One lane of laneCounts.
    void triggerEvent(bool external) override;
    void freezeRecord() override;
    // A split ring's frozen window from the lane counts and the copies served (HistoryCopy::finish);
    // false when the copies are out of step with the lanes.
    bool splitWindow(const uint64_t (&lanes)[4], const uint32_t (&served)[4]);
    void historyFault();
    void traceLead(); // Trace builds: samples the copies' lead over the lanes (Tag::CaptureLead).
    void publishTriggered();
    static CaptureDma *active_;
    PeripheralResources &resources_;
    AnalogFrontend &frontend_;
    PeripheralLease lease_;
    AdcClock clock_;
    TriggerGate gate_;
    FrameMailbox mailbox_;
    FrameMetadata metadata_;
    volatile uint32_t complete_ = 0;
    volatile uint32_t completed_ = 0;
    volatile DataStatus status_ = DataStatus::NoData;
    uint32_t deadline_ = 0;
    volatile uint64_t cycles_[4]{};
    DmaCycleGuard cycleGuard_;
    bool rolling_=false,gated_=false;
    volatile bool frozen_=false;
    bool forced_=false; // This capture's gate event was forced (force()).
    volatile bool stopFailed_=false; // A stream was still enabled after quiesce().
    bool adcStopPending_=false; // stop(true) failed: the retry that proves the streams stopped stops the ADC clock.
    // A failed stop is tried again at this spacing, not every poll: each try spins in DmaStream::disable
    // for every stream (HIL 2026-11-10: 860 tries in 0.35 s kept the main loop busy).
    static constexpr uint32_t StopRetryMs=50;
    uint32_t stopRetryAt_=0;
    uint32_t requestedSamples_=0;
    uint32_t slack_=0; // Points the gate keeps on each side of the event (TriggerRecord::slack).
    uint64_t eventCursor_=0;
    bool edgeless_=false;
    uint32_t edgelessSequence_=0;
    uint64_t edgelessTimestamp_=0;
    TriggerSettings trigger_;
    CaptureBuffer buffer_;
    CircularProgress::Window stopped_;
    HistoryCopy history_;
    SplitHistory split_; // Where a frozen split ring keeps each lane's words (publishTriggered).
    PeripheralLease historyLease_; // The pacing streams while a split ring is captured (ScopeHistory).
    uint32_t leadLow_[4]{UINT32_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX},leadSamples_=0,leadFaults_=0;
};
} // namespace openosc::scope
