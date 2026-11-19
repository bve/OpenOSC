#include "openosc/instrument/data_access.hpp"
#include "openosc/acquisition/rolling_progress.hpp"

namespace openosc {
void DataAccess::serviceRolling(uint32_t now) {
    if (!rollingActive_ || !active_ || stopping_ || scopeHeld_ ||
        measurement_!=MeasurementMode::Oscilloscope || captureFailed_ || store_.busy()) return;
    const auto slice=scope::RollingProgress::next(capture_.rollingAvailable(),rollingConsumed_);
    if (slice.count) {
        if (slice.overflow) { rolling_.reset(); rollingLost_=true; }
        auto metadata=capture_.metadata();
        metadata.origin=uint32_t(slice.first%scope::RollingProgress::Ring);
        metadata.samples=slice.count;
        metadata.timestampUs+=uint64_t(double(slice.first)*1000000/metadata.samplesPerSecond.value());
        scope::CaptureBuffer ring; scope::AcquisitionArena::roll(ring);
        const uint16_t *banks[4]={ring.lanes[0],ring.lanes[1],ring.lanes[2],ring.lanes[3]};
        const scope::FrameView frame(metadata,banks,ring.wordsPerLane());
        if (!rolling_.append(frame)) {
            stopCapture(false); captureFailed_=true; captureRetryAt_=now+CaptureRetryMs; status_=DataStatus::HardwareError; return;
        }
        if (rolling_.size()==rolling_.capacity()) rollingLost_=false;
        status_=rollingLost_ ? DataStatus::Overflow : DataStatus::Ok;
    }
    if (!rolling_.size() || rollingConsumed_==rollingPublished_ || !store_.writable() || uint32_t(now-rollingAt_)<16) return;
    const auto snapshot=rolling_.snapshot();
    auto metadata=snapshot.metadata(); metadata.sequence=++sequence_;
    const uint16_t *banks[]={snapshot.bank(0),nullptr,nullptr,nullptr};
    if (store_.beginLinear({metadata,banks,rolling_.capacity()},now)) {
        rollingAt_=now; rollingPublished_=rollingConsumed_;
    }
}
} // namespace openosc
