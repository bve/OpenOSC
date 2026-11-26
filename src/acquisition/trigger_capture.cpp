#include "openosc/acquisition/capture.hpp"
#include "openosc/acquisition/capture_faults.hpp"
#include "openosc/acquisition/trigger_record.hpp"
#include "openosc/acquisition/dma_lane_progress.hpp"
#include "openosc/acquisition/history_schedule.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/critical_section.hpp"
#include "openosc/platform/dma_stream.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "capture_lanes.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::scope {
void CaptureDma::triggerEvent(bool external) {
    CriticalSection guard;
    uint64_t lanes[4]; laneCounts(lanes);
    eventCursor_=CircularProgress::prefix(lanes);
    comparatorEvent_=external;
}
void CaptureDma::freezeRecord() {
    CriticalSection guard;
    // No new CC event; requests already raised are still served (capture_lanes.hpp, RM0455 15.3.19).
    capture_dma::stopEvents();
    uint64_t lanes[4]; uint32_t remaining[4];
    laneCounts(lanes,remaining); // Snapshot genuine pending wrap flags before EN=0 produces its own TCIF.
    for (unsigned lane=0;lane<4;++lane) {
        auto *stream=capture_dma::streams[lane];
        if (!DmaStream::disable(stream)) {
            captureFault(1U << 17 | lane << 8 | (stream->FCR & 0x3fU));
            abandon(DataStatus::HardwareError); return; // Keeps the memory unless every stream stopped.
        }
        capture_dma::releaseRequest(lane); // Its stream reads EN 0 with its FIFO flushed.
        lanes[lane]+=DmaLaneProgress::drained(remaining[lane],stream->NDTR,buffer_.segmentTransfers);
        // Packed bytes reach RAM in pairs: a trailing odd byte may still be in the FIFO.
        if (buffer_.format==SampleFormat::Bytes) lanes[lane]&=~uint64_t(1);
    }
    stopped_=CircularProgress::window(lanes,buffer_.laneTransfers);
    if (buffer_.split()) {
        uint32_t served[4]{};
        if (!history_.finish(served)) { // The copies' phase, read from stopped MDMA channels.
            captureFault(1U << 19 | 0x1ffU | 0x1ffU << 23);
            abandon(DataStatus::Overflow); return;
        }
        if (!splitWindow(lanes,served)) { abandon(DataStatus::Overflow); return; }
    }
    if (!quiesce(false)) return; // Still Capturing: the memory stays with the capture until a stop succeeds.
    frozen_=true;
}
bool CaptureDma::splitWindow(const uint64_t (&lanes)[4],const uint32_t (&served)[4]) {
    if (HistorySchedule::window(buffer_,lanes,served,stopped_,split_)) {
        const uint32_t cycle=HistorySchedule::cycle(buffer_);
        uint32_t behind=uint32_t(lanes[0]%buffer_.copyTransfers)&0xffU;
        for (unsigned lane=1;lane<4;++lane) behind|=((served[0]+cycle-served[lane])%cycle&0xffU)<<(8*lane);
        usbtrace::mark(usbtrace::Tag::CaptureCopies,behind);
        return true;
    }
    const uint32_t made=uint32_t(lanes[0]/buffer_.copyTransfers%HistorySchedule::cycle(buffer_));
    captureFault(1U << 19 | (served[0] & 0x1ffU) | (made & 0x1ffU) << 23);
    return false;
}
uint32_t CaptureDma::autoWaitMs(uint32_t samples, Rational rate) {
    if (!rate.valid()) return 1000;
    const uint64_t ms=uint64_t(samples)*1000*rate.denominator/rate.numerator;
    return uint32_t(std::clamp<uint64_t>(ms,20,1000));
}
bool CaptureDma::force() {
    if (!gated_ || stopFailed_ || mailbox_.state()!=FrameMailbox::State::Capturing) return false;
    if (frozen_) return true; // Past its event: published on the next service().
    const bool before=forced_;
    forced_=true; // First: in Armed the event runs inside force().
    const auto result=gate_.force();
    if (result!=TriggerGate::Force::Forced) forced_=before; // An edge or an earlier force came first.
    return result!=TriggerGate::Force::Refused;
}
void CaptureDma::publishTriggered() {
    frozen_=false; edgeless_=false;
    if (stopped_.size()<4 || stopped_.size()>buffer_.transferRing()) {
        captureFault(1U << 22 | uint32_t(std::min<uint64_t>(stopped_.size(), 0x3fffff)));
        mailbox_.cancel(); status_=DataStatus::Overflow; return;
    }
    // The DMA cursors count transfers (the first missing one ends a window); the record counts
    // points, pointsPerTransfer() of them per transfer: transfer t holds points k*t .. k*t+k-1.
    const uint64_t k=buffer_.pointsPerTransfer();
    const uint64_t first=stopped_.first*k, end=stopped_.end*k, event=eventCursor_*k;
    auto metadata=metadata_;
    metadata.samples=uint32_t(end-first); metadata.origin=uint32_t(first%metadata.ring);
    metadata.clearTrigger(); metadata.forced=forced_;
    const double offset=double(first)*1000000/metadata.samplesPerSecond.value();
    if (!std::isfinite(offset) || offset>=double(UINT64_MAX-metadata.timestampUs)) {
        mailbox_.cancel(); status_=DataStatus::InvalidArgument; return;
    }
    metadata.timestampUs+=uint64_t(offset);
    const uint16_t *banks[4]={buffer_.lanes[0],buffer_.lanes[1],buffer_.lanes[2],buffer_.lanes[3]};
    const FrameView ring(metadata,banks,buffer_.wordsPerLane(),buffer_.split() ? &split_ : nullptr);
    const uint32_t target=event<=first ? 0 : uint32_t(std::min<uint64_t>(metadata.samples-1,event-first));
    const bool waited=!autoWaiting_ ||
        Board::milliseconds()-autoSince_>=autoWaitMs(requestedSamples_,metadata.samplesPerSecond);
    auto frame=TriggerRecord::select(ring,trigger_,requestedSamples_,target,forced_,comparatorEvent_,waited);
    if (!frame.valid()) {
        if (trigger_.qualified()) ++misses_;
        mailbox_.cancel(); status_=DataStatus::NoData; return; // A comparator event must agree with the raw edge.
    }
    // Trace: how late the freeze landed after the record the trigger asked for (bits 0-15, bit 31 when
    // early) and the ring points left before the record (bits 16-30): the stop headroom in use.
    const uint32_t pre=uint32_t(uint64_t(requestedSamples_-1)*trigger_.position/1000);
    const uint64_t due=event+(requestedSamples_-1-pre)+slack_;
    const uint64_t late=end>=due ? end-due : due-end;
    const uint32_t spare=(frame.metadata().origin+metadata.ring-metadata.origin)%metadata.ring;
    usbtrace::mark(usbtrace::Tag::CaptureStop, uint32_t(std::min<uint64_t>(late,0xffff)) | std::min<uint32_t>(spare,0x7fffU) << 16 |
                   (end<due ? 1U << 31 : 0U));
    if (comparatorEvent_ && frame.metadata().triggered && !frame.metadata().forced) { // Where the comparator event fell.
        const uint32_t size=ring.metadata().ring ? ring.metadata().ring : 1;
        const uint32_t start=uint32_t((uint64_t(frame.metadata().origin)+size-ring.metadata().origin%size)%size);
        frame=frame.withEvent(int32_t(int64_t(target)-start-frame.metadata().triggerIndex));
    }
    metadata_=frame.metadata();
    // An Auto or forced record without an accepted edge: retain the capture's decision for readers.
    edgeless_=!frame.metadata().triggered;
    edgelessSequence_=frame.metadata().sequence; edgelessTimestamp_=frame.metadata().timestampUs;
    __DMB();
    if (mailbox_.publish(frame)) { ++completed_; status_=DataStatus::Ok; autoWaiting_=false; }
    else { mailbox_.cancel(); status_=DataStatus::InvalidArgument; }
}
} // namespace openosc::scope
