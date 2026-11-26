#include "openosc/instrument/data_access.hpp"
#include "openosc/acquisition/rolling_progress.hpp"

namespace openosc {
void DataAccess::serviceRolling(uint32_t now) {
    // A trend is written in poll alone, never while a frame is drawn: the ring holds 200 to 400 s (by channels and record memory), so nothing is lost.
    const bool trend=request_.trend;
    if (!active_ || stopping_ || measurement_!=MeasurementMode::Oscilloscope || ((trend || trendOwed()) && backgroundPass_))
        return;
    if (trendOwed()) { finishTrend(now); return; } // Held back at its STOP by a reader of the frame on show.
    if (!rollingActive_ || scopeHeld_ || captureFailed_ || store_.busy()) return;
    auto metadata=capture_.metadata();
    const auto slice=scope::RollingProgress::next(capture_.rollingAvailable(),rollingConsumed_,metadata.ring);
    if (slice.count) {
        if (slice.overflow) { rolling_.reset(); rollingLost_=true; if (trend) trend_.reset(meterReadings_.quantity()); }
        metadata.origin=uint32_t(slice.first%metadata.ring);
        metadata.samples=slice.count;
        metadata.timestampUs+=uint64_t(double(slice.first)*1000000/metadata.samplesPerSecond.value());
        scope::CaptureBuffer ring;
        scope::AcquisitionArena::roll(ring, metadata.layout == scope::Layout::InterleavedCh1);
        const uint16_t *banks[4]={ring.lanes[0],ring.lanes[1],ring.lanes[2],ring.lanes[3]};
        const scope::FrameView frame(metadata,banks,ring.wordsPerLane());
        if (!rolling_.append(frame) || (trend && !trend_.append(frame))) {
            stopCapture(false); captureFailed_=true; captureRetryAt_=now+CaptureRetryMs; status_=DataStatus::HardwareError; return;
        }
        if (rolling_.size()==rolling_.capacity()) rollingLost_=false;
        status_=rollingLost_ ? DataStatus::Overflow : DataStatus::Ok;
    }
    // A trend at its limit (TrendBuffer::full) stops as STOP stops it, which publishes what is not on show yet.
    // Only a running capture: after RUN the full buffer waits for startCapture's reset, not for another stop.
    if (trend && trend_.full() && capture_.state()==scope::FrameMailbox::State::Capturing) {
        trendLimit_=true; holdScope(true); return;
    }
    // Published when a point was stored (an averaged Roll stores one per Timebases::averaging consumed), a
    // trend's when its copy on show is behind its record (TrendBuffer::behind).
    const bool due=trend ? trend_.behind() : rolling_.size() && rolling_.dropped()+rolling_.size()!=rollingPublished_;
    if (due && store_.writable() && uint32_t(now-rollingAt_)>=16) publishRolling(trend,now);
}
// The frame of Roll's history, or the trend's (its means) with the trend's copy on show (TrendBuffer::publish).
void DataAccess::publishRolling(bool trend,uint32_t now) {
    // Roll's history keeps the channels' metadata and the rate.
    const auto snapshot=trend ? trend_.snapshot(rolling_.snapshot().metadata()) : rolling_.snapshot();
    auto metadata=snapshot.metadata(); metadata.sequence=sequence_+1;
    const uint16_t *banks[]={snapshot.bank(0),nullptr,nullptr,nullptr};
    if (!store_.beginLinear({metadata,banks,trend ? scope::TrendBuffer::Capacity : rolling_.capacity()},now)) return;
    ++sequence_; rollingAt_=now;
    if (!trend) { rollingPublished_=rolling_.dropped()+rolling_.size(); return; }
    trend_.publish(metadata);
    // The record's own time/div and rate are the plan's now (:TIM:SCAL?, :ACQ:SRAT?).
    configuration_.timebaseNs=metadata.timebaseNs; configuration_.samplesPerSecond=metadata.samplesPerSecond;
}
// The record STOP ended (TrendBuffer::stop) replaces the frame on show at the STOP itself (holdScopeCore): it
// is the frame HOLD holds and every reader is given. HOLD's own lease gives way; a reader of the frame on show
// that is not ours (a file export, a USB host) holds the record back until it lets go, and serviceRolling
// publishes it then. Meanwhile acquireScope gives nothing, so the background readers the hold revoked take
// nothing new and no reader gets the frame from before the STOP.
void DataAccess::finishTrend(uint32_t now) {
    if (!trendOwed() || store_.busy() || store_.readers()!=(heldFrame_.valid() ? 1U : 0U)) return;
    heldFrame_.release();
    publishRolling(true,now);
}
} // namespace openosc
