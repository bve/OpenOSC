#include "openosc/acquisition/capture.hpp"
#include "openosc/acquisition/rolling_progress.hpp"
#include "openosc/acquisition/dma_lane_progress.hpp"
#include "openosc/platform/critical_section.hpp"
#include "openosc/platform/board.hpp"
#include "capture_lanes.hpp"

namespace openosc::scope {
using namespace capture_dma;
CaptureProgress CaptureDma::progress() const {
    CriticalSection guard;
    CaptureProgress result;
    result.sequence=metadata_.sequence; result.status=status_;
    result.active=lease_.valid() && (rolling_ || gated_) && !frozen_ &&
                  mailbox_.state()==FrameMailbox::State::Capturing;
    if (result.active) {
        uint64_t lanes[4]; laneCounts(lanes);
        result.words=CircularProgress::prefix(lanes);
    }
    return result;
}
uint64_t CaptureDma::rollingAvailable() const {
    if (!rolling_ || mailbox_.state()!=FrameMailbox::State::Capturing) return 0;
    uint64_t lanes[4];
    CriticalSection guard;
    laneCounts(lanes);
    return RollingProgress::available(lanes) * buffer_.pointsPerTransfer();
}
void CaptureDma::laneCounts(uint64_t (&lanes)[4],uint32_t *remaining) const {
    for (unsigned lane=0;lane<4;++lane) {
        DMA_TypeDef *dma=controller(lane);
        const uint32_t flag=0x20U<<(lane<2 ? 6 : 0);
        uint32_t flags=dma->LISR,count=streams[lane]->NDTR;
        const uint32_t after=dma->LISR;
        if ((flags^after)&flag) { flags=after; count=streams[lane]->NDTR; }
        lanes[lane]=DmaLaneProgress::completed(cycles_[lane],count,flags&flag,buffer_.segmentTransfers);
        if (remaining) remaining[lane]=count;
    }
    __DMB();
}
} // namespace openosc::scope
