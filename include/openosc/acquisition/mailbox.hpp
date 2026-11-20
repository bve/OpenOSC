#pragma once
#include "openosc/acquisition/frame.hpp"
#include <atomic>

namespace openosc::scope {
class FrameMailbox;
class FrameLease {
public:
    FrameLease() = default;
    ~FrameLease();
    FrameLease(const FrameLease &) = delete;
    FrameLease &operator=(const FrameLease &) = delete;
    FrameLease(FrameLease &&other) noexcept;
    FrameLease &operator=(FrameLease &&other) noexcept;
    const FrameView &view() const { return view_; }
    bool valid() const { return owner_ != nullptr; }
    void release();
    // Another lease of the same record (the same owner, this lease's annotated view): it keeps the record
    // as this one does, released on its own. False when this one is not valid or `out` already is.
    // Foreground only, as acquire/release.
    bool share(FrameLease &out) const;
    bool annotateTrigger(bool found, uint32_t index, TriggerEdge edge = {}) {
        if (!valid())
            return false;
        auto next = view_.withTrigger(found, index, edge);
        if (!next.valid())
            return false;
        view_ = next;
        return true;
    }

private:
    friend class FrameMailbox;
    FrameView view_;
    FrameMailbox *owner_ = nullptr;
};
class FrameMailbox {
public:
    enum class State : uint32_t { Idle, Capturing, Ready, Leased };
    bool begin();
    bool publish(const FrameView &frame);
    bool acquire(FrameLease &lease);
    bool cancel();
    State state() const { return state_.load(std::memory_order_acquire); }
    uint32_t readers() const { return readers_; }

private:
    friend class FrameLease;
    void release();
    std::atomic<State> state_{State::Idle};
    uint32_t readers_ = 0; // Lease management belongs to the single foreground consumer context.
    FrameView frame_;
};
} // namespace openosc::scope
