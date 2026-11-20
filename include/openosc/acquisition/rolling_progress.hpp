#pragma once
#include <algorithm>
#include <cstdint>
#include "openosc/acquisition/circular_progress.hpp"

namespace openosc::scope {
class RollingProgress {
public:
    // One foreground turn must also drain arrivals during a complete LVGL frame
    // at the maximum Roll rate (~32768 words/s), including short storage stalls.
    static constexpr uint32_t Ring=AcquisitionArena::RollRingPoints,Chunk=4096,Guard=512;
    struct Slice { uint64_t first=0; uint32_t count=0; bool overflow=false; };
    // Each DMA lane supplies every fourth chronological word. A prefix may
    // contain one, two or three words of the next group, even at slow rates.
    static uint64_t available(const uint64_t (&lanes)[4]) {
        const uint64_t missing=CircularProgress::prefix(lanes);
        return missing ? missing-1 : 0; // Leave the newest DMA write time to become globally visible.
    }
    static Slice next(uint64_t available,uint64_t &consumed,uint32_t ring = Ring) {
        if (available<=consumed) return {};
        Slice result;
        if (available-consumed>ring-Guard) { consumed=available-(ring-Guard); result.overflow=true; }
        result.first=consumed;
        result.count=uint32_t(std::min<uint64_t>(available-consumed,Chunk));
        consumed+=result.count;
        return result;
    }
};
} // namespace openosc::scope
