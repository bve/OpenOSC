#pragma once
#include <cstdint>

namespace openosc::companion {
// Reserve time only at a record boundary. Even a stuck transfer must give the ADC its next turn.
class CaptureWindow {
public:
    bool defer(uint32_t now, bool work, bool streaming) {
        const uint32_t elapsed = now - started_;
        if (open_ && work && elapsed < WindowMs) return true;
        open_ = false;
        if (!work || (granted_ && elapsed < (streaming ? StreamIntervalMs : IdleIntervalMs))) return false;
        started_ = now;
        granted_ = open_ = true;
        return true;
    }
    void release() { open_ = false; }
private:
    // A full request takes up to 2.7 ms. The DMA receive window takes 4.8 ms.
    static constexpr uint32_t WindowMs = 12, StreamIntervalMs = 24, IdleIntervalMs = 120;
    uint32_t started_ = 0;
    bool open_ = false, granted_ = false;
};
}
