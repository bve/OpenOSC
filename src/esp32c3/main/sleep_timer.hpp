#pragma once
#include <cstdint>

namespace openosc::companion {
class SleepTimer {
public:
    explicit SleepTimer(uint32_t now) : lastUse_(now) {}

    void activity(uint32_t now) {
        if (reason_ == Reason::Manual) return;
        lastUse_ = now;
        reason_ = Reason::None;
    }
    void poll(uint32_t now, uint8_t minutes, bool inUse) {
        if (reason_ == Reason::Manual) return;
        if (inUse || !minutes) activity(now);
        else if (!pending() && now - lastUse_ >= uint32_t(minutes) * 60000u) {
            reason_ = Reason::Idle;
            requestedAt_ = now;
        }
    }
    void request(uint32_t now) {
        if (reason_ == Reason::Manual) return;
        reason_ = Reason::Manual;
        requestedAt_ = now;
    }
    bool pending() const { return reason_ != Reason::None; }
    bool due(uint32_t now) const { return pending() && now - requestedAt_ >= ReplyGraceMs; }

private:
    enum class Reason : uint8_t { None, Idle, Manual };
    static constexpr uint32_t ReplyGraceMs = 1250;
    uint32_t lastUse_, requestedAt_ = 0;
    Reason reason_ = Reason::None;
};
}
