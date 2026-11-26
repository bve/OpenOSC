#pragma once
#include <cstdint>

namespace openosc::usb {
// Settings > Files & USB > USB controller > Auto (GitHub #170): the controller runs only while a
// computer is there. Pure policy over modular milliseconds; ApplicationController::serviceUsbAuto is
// its adapter (InstrumentUsb, the battery's debounced PC9, Remote) and applies poll()'s answer with
// InstrumentUsb::enable, so the existing quiet attach and clock rules do the attaching.
// A computer counts when InstrumentUsb::hostPresent sees SOFs. PC9, the controller state,
// a stale USB configuration and Remote do not establish host presence.
// - Probing starts at boot, when Auto takes over an active or pending link, and on a PC9 rise.
//   A rise while off resets the retry delays. The controller goes on and the attempt fails
//   NoHostMs after it is actually on the bus with no host, or
//   AttachWaitMs after it was asked for while its attach still waits for a quiet capture. A rise
//   during an attempt restarts its window, so a cable plugged in near its end has the whole time.
//   A restricted clock (InstrumentUsb's ClockPolicy) defers the attempt: it is owed until the clock
//   allows the link, and no time of it is used meanwhile.
// - After a failed attempt, while PC9 stays high (a charger or a pin that sticks, GitHub #104):
//   another attempt RetryMs[0], [1], then [2] (each time) after the controller went off. PC9 low
//   (on battery) ends the retries at once; the next rise attempts at once. begin() without probeNow
//   (Auto entered with the controller off) attempts RetryMs[0] later with PC9 high, at a rise else.
// - Host (a computer seen): on while PC9 is high, or the host is active, with no timeout (a suspended
//   computer keeps its session); off after UnpluggedMs with neither. A host seen restarts the retry
//   delays. SOFs are definitive: an active host is never switched off, whatever PC9 reads, so a pin
//   that reads low does not drop a working link.
// - Remote keeps the controller on while it runs (no attempt fails, no Host is ended; an Off state
//   becomes an attempt) but is no host: when it ends, an attempt has its NoHostMs (or AttachWaitMs)
//   from then. Times are modular (uint32_t differences): every wait is far below 2^31 ms.
class AutoPolicy {
public:
    static constexpr uint32_t NoHostMs = 5000, AttachWaitMs = 30000, UnpluggedMs = 5000;
    static constexpr uint32_t RetryMs[3] = {30000, 60000, 120000};
    enum class State : uint8_t { Off, Probing, Host };
    struct Inputs {
        uint32_t now = 0;
        bool external = false; // PC9 (BatteryState::external, debounced).
        bool clock = false;    // The running clock allows the link (InstrumentUsb::clockAllows).
        bool onBus = false;    // Actually attached (InstrumentUsb::onBus), not merely enabled or pending.
        bool host = false;     // InstrumentUsb::hostPresent: SOFs at the last poll, never CONFIGURED alone.
        bool remote = false;   // USB Remote runs.
        // PC9 debounced at least once (BatteryMonitor::externalKnown). Until then `external` is no
        // observation: no edge, no retry ended. The first known level only seeds the edge detector
        // (an attempt keeps its window); high while off with nothing owed, the first retry is due
        // RetryMs[0] after the controller went off.
        bool externalKnown = true;
    };
    // A fresh start, no retry history, no host seen. probeNow: an attempt owed at once; else none
    // until a PC9 rise, or the first retry delay while `external` (PC9 as sampled now) is high.
    // `external` also seeds the edge detector, so the first poll never sees a rise that was not.
    // `known`: whether `external` is a debounced observation (Inputs::externalKnown).
    void begin(uint32_t now, bool probeNow = true, bool external = false, bool known = true) {
        *this = AutoPolicy{};
        owed_ = probeNow;
        known_ = known;
        external_ = known && external;
        retrying_ = !probeNow && external_;
        offAt_ = now;
    }
    // The controller's state for this pass: true on.
    bool poll(const Inputs &in) {
        const uint32_t now = in.now;
        const bool first = in.externalKnown && !known_;
        const bool rise = in.externalKnown && known_ && in.external && !external_;
        if (in.externalKnown) { known_ = true; external_ = in.external; }
        if (first && in.external && state_ == State::Off && !owed_) retrying_ = true; // From offAt_.
        switch (state_) {
        case State::Off:
            if (rise) { owed_ = true; failures_ = 0; }                 // A fresh attempt.
            if (known_ && !external_) { retrying_ = false; failures_ = 0; } // On battery: no retries.
            if (retrying_ && uint32_t(now - offAt_) >= delay()) owed_ = true;
            if (!(owed_ && in.clock) && !in.remote) break; // Remote keeps it on: an attempt from its end.
            owed_ = retrying_ = false;
            state_ = State::Probing;
            askedAt_ = now; bus_ = false;
            [[fallthrough]]; // The same pass: a controller already on the bus, or a host, counts from now.
        case State::Probing:
            if (in.host) { state_ = State::Host; failures_ = 0; heldAt_ = now; break; }
            if (rise) { askedAt_ = busAt_ = now; failures_ = 0; } // Plugged in now: the whole window.
            if (!in.clock && !in.remote) { switchOff(now); owed_ = true; retrying_ = false; break; } // Deferred.
            if (in.remote) { askedAt_ = busAt_ = now; bus_ = in.onBus; break; }
            if (in.onBus) {
                if (!bus_) { bus_ = true; busAt_ = now; }
                if (uint32_t(now - busAt_) >= NoHostMs) fail(now, external_);
            } else {
                if (bus_) { bus_ = false; askedAt_ = now; } // Off the bus again (a profile change): a new wait.
                if (uint32_t(now - askedAt_) >= AttachWaitMs) fail(now, external_);
            }
            break;
        case State::Host: // Held by PC9, the host or Remote; off UnpluggedMs after the last pass with any.
            if (in.host || external_ || in.remote) heldAt_ = now;
            else if (uint32_t(now - heldAt_) >= UnpluggedMs) { switchOff(now); retrying_ = false; failures_ = 0; }
            break;
        }
        return state_ != State::Off;
    }
    State state() const { return state_; }

    bool owed() const { return owed_; }           // An attempt waits (the clock, tests).
    bool retrying() const { return retrying_; }   // A retry is scheduled.
    // The wait before the next retry: 30 s after the first failure, 60 s after the second, then 120 s.
    uint32_t delay() const { return RetryMs[failures_ >= 3 ? 2 : failures_ ? failures_ - 1 : 0]; }

private:
    void switchOff(uint32_t now) { state_ = State::Off; offAt_ = now; bus_ = false; }
    void fail(uint32_t now, bool external) {
        switchOff(now);
        retrying_ = external;
        if (failures_ < 255) ++failures_;
        if (!external) failures_ = 0;
    }
    State state_ = State::Off;
    uint32_t askedAt_ = 0, busAt_ = 0, offAt_ = 0, heldAt_ = 0;
    uint8_t failures_ = 0;
    bool owed_ = false, retrying_ = false, bus_ = false, external_ = false, known_ = true;
};
} // namespace openosc::usb
