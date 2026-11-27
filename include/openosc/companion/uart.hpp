#pragma once
#include "openosc/companion/protocol.hpp"

namespace openosc::companion {
class Uart {
public:
    enum class Result { Idle, Busy, Done, Interrupted, Fault };
    bool open();
    bool probeOwnsClock() const;
    static bool debugAllowed();
    void close();
    void wake(bool high);
    bool waking() const { return wakeHigh_; }
    bool start(const uint8_t *data, unsigned size, bool collectReply = false);
    Result poll(Frame &reply);
    static void yieldBus();
private:
    void collect();
    void stopHardware();
    void finish(Result result);
    inline static Uart *owner_ = nullptr;
    char *scratch_ = nullptr;
    uint32_t clockHz_ = 0, started_ = 0;
    unsigned txWords_ = 0;
    Result result_ = Result::Idle;
    Frame reply_{};
    bool receiving_ = false, wakeHigh_ = false;
};
}
