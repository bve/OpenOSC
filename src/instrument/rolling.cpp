#include "openosc/instrument/data_access.hpp"
#include "openosc/acquisition/rolling_progress.hpp"

namespace openosc {
void DataAccess::serviceRolling(uint32_t now) {
    // A trend is written in poll alone, never while a frame is drawn: the ring holds 200 to 400 s (by channels and record memory), so nothing is lost.
    if (!rollingActive_ || !active_ || stopping_ || scopeHeld_ ||
        measurement_!=MeasurementMode::Oscilloscope || captureFailed_ || store_.busy() || (request_.trend && backgroundPass_)) return;
    auto metadata=capture_.metadata();
    const auto slice=scope::RollingProgress::next(capture_.rollingAvailable(),rollingConsumed_,metadata.ring);
    if (slice.count) {
        if (slice.overflow) { rolling_.reset(); rollingLost_=true; if (request_.trend) trend_.reset(meterReadings_.quantity()); }
        metadata.origin=uint32_t(slice.first%metadata.ring);
        metadata.samples=slice.count;
        metadata.timestampUs+=uint64_t(double(slice.first)*1000000/metadata.samplesPerSecond.value());
        scope::CaptureBuffer ring;
        scope::AcquisitionArena::roll(ring, metadata.layout == scope::Layout::InterleavedCh1);
        const uint16_t *banks[4]={ring.lanes[0],ring.lanes[1],ring.lanes[2],ring.lanes[3]};
        const scope::FrameView frame(metadata,banks,ring.wordsPerLane());
        if (!rolling_.append(frame) || (request_.trend && !trend_.append(frame))) {
            stopCapture(false); captureFailed_=true; captureRetryAt_=now+CaptureRetryMs; status_=DataStatus::HardwareError; return;
        }
        if (rolling_.size()==rolling_.capacity()) rollingLost_=false;
        status_=rollingLost_ ? DataStatus::Overflow : DataStatus::Ok;
    }
    // Published when a point was stored (an averaged Roll stores one per Timebases::averaging consumed;
    // a trend counts a point closed and a restart of its DMM part).
    const bool trend=request_.trend;
    const uint64_t stored=trend ? trend_.revision() : rolling_.dropped()+rolling_.size();
    if (!(trend ? trend_.size() : rolling_.size()) || stored==rollingPublished_ || !store_.writable() || uint32_t(now-rollingAt_)<16) return;
    // The trend's frame is its means; Roll's history keeps the channels' metadata and the rate.
    const auto snapshot=trend ? trend_.snapshot(rolling_.snapshot().metadata()) : rolling_.snapshot();
    metadata=snapshot.metadata(); metadata.sequence=++sequence_;
    const uint16_t *banks[]={snapshot.bank(0),nullptr,nullptr,nullptr};
    if (store_.beginLinear({metadata,banks,trend ? scope::TrendBuffer::Capacity : rolling_.capacity()},now)) {
        rollingAt_=now; rollingPublished_=stored;
        if (trend) {
            trend_.publish(metadata);
            // The record's own time/div and rate are the plan's now (:TIM:SCAL?, :ACQ:SRAT?).
            configuration_.timebaseNs=metadata.timebaseNs; configuration_.samplesPerSecond=metadata.samplesPerSecond;
        }
    }
}
} // namespace openosc
