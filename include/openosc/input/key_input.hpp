#pragma once
#include "openosc/input/gestures.hpp"
#include "openosc/platform/input.hpp"
#include <atomic>

namespace openosc {
struct TimedKeyEvents { KeyEvents events; uint32_t at=0; };
// One sampling producer and one foreground consumer. Stroke classification
// keeps its original time even when a Flash operation delays the consumer.
class KeyInput {
public:
    static constexpr uint32_t Capacity=128;
    void reset(uint32_t keys,uint32_t now) {
        gestures_=KeyGestures{}; gestures_.poll(keys,now);
        written_=read_=0; keys_=keys; activity_=false; dropped_=0; holding_=false;
    }
    // A wake takes the stroke of every key held. With relativeHold (the power-off hold settings) REL/Power
    // still counts towards its hold (power off from a dark screen); its short release gives nothing.
    // Without it (the short-press power off) REL's hold is REL's own function, so the wake takes it too.
    void wake(uint32_t keys,uint32_t now,bool relativeHold) {
        reset(keys,now);
        if (relativeHold) gestures_.holdOnly(Keypad::Relative,now);
    }
    void sample(uint32_t keys,uint32_t now) {
        if (keys_.exchange(keys,std::memory_order_relaxed)!=keys) activity_.store(true,std::memory_order_release);
        const auto events=gestures_.poll(keys,now);
        uint32_t held=0;
        const bool holding=gestures_.holding(Keypad::Relative,now,held);
        holdMs_.store(held,std::memory_order_relaxed); holding_.store(holding,std::memory_order_release);
        if (!events.any()) return;
        const uint32_t written=written_.load(std::memory_order_relaxed);
        if (uint32_t(written-read_.load(std::memory_order_acquire))==Capacity) {
            dropped_.fetch_add(1,std::memory_order_relaxed); return;
        }
        queue_[written&(Capacity-1)]={events,now};
        written_.store(written+1,std::memory_order_release);
    }
    bool take(TimedKeyEvents &event) {
        const uint32_t read=read_.load(std::memory_order_relaxed);
        if (read==written_.load(std::memory_order_acquire)) return false;
        event=queue_[read&(Capacity-1)]; read_.store(read+1,std::memory_order_release);
        return true;
    }
    uint32_t keys() const { return keys_.load(std::memory_order_acquire); }
    // REL/Power held towards power off: how long, as of the last sample.
    bool powerHold(uint32_t &elapsed) const {
        if (!holding_.load(std::memory_order_acquire)) return false;
        elapsed=holdMs_.load(std::memory_order_relaxed); return true;
    }
    bool activity() { return activity_.exchange(false,std::memory_order_acq_rel); }
    uint32_t dropped() { return dropped_.exchange(0,std::memory_order_acq_rel); }
private:
    KeyGestures gestures_;
    TimedKeyEvents queue_[Capacity];
    std::atomic<uint32_t> written_{0},read_{0},keys_{0},dropped_{0},holdMs_{0};
    std::atomic<bool> activity_{false},holding_{false};
};
} // namespace openosc
