#pragma once
#include <cstdint>

namespace openosc::usb {
// The scope capture as the USB attach sees it: quiet (true) asks a fast capture to stop and fast
// ones not to start; the answer is true only once that holds (a capture that did not stop, or one
// a pending HOLD waits for, answers false). quiet false lets captures run as planned (always true).
class QuietClient {
public:
    virtual bool quietCapture(bool quiet) = 0;
protected:
    ~QuietClient() = default;
};

// When the capture must be quiet for a host's enumeration: one bounded window per logical attach.
// Full-rate capture DMA during enumeration coincided with failed CDC enumeration (descriptor -110)
// and a host controller that gave up (HIL 2026-11-15, Normal on DC at 200/100).
// - request(): a new logical attach is asked for (a planned reattach, a start after a full stop; the
//   boot is one). attach(now) then opens its window of BudgetMs once; further attach() calls of the
//   same request (a start retried after a failure) open nothing.
// - abandon(): the request failed (initialization or connect): its window ends, nothing reopens it
//   until a new request.
// - reset(now): a bus reset or the address watchdog's retry. It never extends or reopens the window
//   of its own episode. Only an episode that ran out without any reset (no host was there) leaves one
//   window for the first reset after it (a host plugged in later); that one spends it.
// - configured(): a healthy host ends the window at once; a later host (unplugged, plugged again)
//   may have one window of its own at its first reset.
// - stopped(): a full stop ends it and allows the next logical attach.
// - connected(now): D+ is up. No bus reset of any kind (answered()) within FirstResetMs ends the window
//   early, as running out does: an episode without a proven reset leaves one window for a later host.
// The budget is a deadline from the window's start: a `now` from before it (a poll's tick taken
// before the window opened) never ends it.
// So a failed host, a host removed after failing and a standalone scope without one get no periodic
// pauses. Modular milliseconds.
class EnumerationQuiet {
public:
    static constexpr uint32_t BudgetMs = 5000;
    static constexpr uint32_t FirstResetMs = 500; // No host's reset this long after the connect: no host (PC9 lies).
    void connected(uint32_t now) { if (active_) { connectedAt_ = now; connecting_ = true; } } // D+ up, at `now`.
    void answered() { connecting_ = false; } // Any bus reset after the connect, SOFs proven or not.
    void request() { armed_ = true; }
    void attach(uint32_t now) {
        if (!armed_) return;
        armed_ = false; awaitingHost_ = false; connecting_ = false; // Its own connect starts the cut-off.
        if (!active_) open(now);
    }
    void abandon() { active_ = false; awaitingHost_ = false; armed_ = false; }
    void reset(uint32_t now) {
        connecting_ = false;
        if (active_) { resetSeen_ = true; return; }
        if (awaitingHost_) { awaitingHost_ = false; open(now); resetSeen_ = true; }
    }
    void configured() { active_ = false; awaitingHost_ = true; }
    void stopped() { active_ = false; awaitingHost_ = false; armed_ = true; }
    bool quiet(uint32_t now) {
        const bool unanswered = connecting_ && int32_t(now - connectedAt_) >= int32_t(FirstResetMs);
        if (active_ && (unanswered || int32_t(now - startedAt_) >= int32_t(BudgetMs))) {
            active_ = connecting_ = false; awaitingHost_ = !resetSeen_;
        }
        return active_;
    }

private:
    void open(uint32_t now) { active_ = true; startedAt_ = now; resetSeen_ = connecting_ = false; }
    uint32_t startedAt_ = 0, connectedAt_ = 0;
    bool active_ = false, resetSeen_ = false, awaitingHost_ = false, armed_ = true; // The boot is a request.
    bool connecting_ = false; // Connected within this window, waiting for the host's first reset.
};

// Every attach of the device to the bus goes through here: the start (USBD_Start after a full
// initialization) and the reconnect (the address watchdog's DevDisconnect/DevConnect). What is
// wanted waits until the capture is proven quiet when the window asks for quiet, and stays waiting
// after a refusal until it is proven (a stopped capture's proof, a pending HOLD's end), also once the
// window has run out: an attach never follows an unproven stop. Then `connect` runs once for it (a
// start supersedes a reconnect). service() runs each poll: it also tells the capture to be quiet or
// not, so captures resume as soon as the window ends (a configured host ends it at once). A start
// whose initialization or connect fails abandons its request: the capture is released at once.
class QuietAttach {
public:
    enum class Kind : uint8_t { None, Reconnect, Start };
    using Connect = bool (*)(void *context, Kind kind);
    QuietAttach(Connect connect, void *context) : connect_(connect), context_(context) {}
    void client(QuietClient *client) { client_ = client; }
    EnumerationQuiet &window() { return window_; }
    void want(Kind kind) { if (kind > pending_) pending_ = kind; }
    bool pending() const { return pending_ != Kind::None; }
    // A full stop: nothing waits any more, the capture may run; the next start is a new request.
    void stop() { window_.stopped(); release(); }
    // A healthy (configured) host: its window ends, and a reconnect still waiting for a quiet capture
    // is over with the episode that asked for it (else a later proof, e.g. a HOLD released, would drop
    // a working link); the capture is released. A waiting start stays: it is the device's own attach.
    void configured() {
        window_.configured();
        if (pending_ == Kind::Reconnect) release();
    }
    // A logical request (InstrumentUsb::reattach): its window opens now, before the attach.
    void request(uint32_t now) { window_.request(); window_.attach(now); }
    // InstrumentUsb::start: `initialized` the USBD set-up's result. True when attached or waiting.
    bool start(uint32_t now, bool initialized) {
        if (!initialized) { abandon(); return false; }
        window_.attach(now); // Opens only for a new request: a retried start does not.
        want(Kind::Start);
        return service(now);
    }
    // False only when a connect ran and failed (the request is abandoned then).
    bool service(uint32_t now) {
        const bool wanted = window_.quiet(now) || (refused_ && pending());
        const bool proven = !client_ || client_->quietCapture(wanted);
        if (!pending()) { refused_ = false; return true; }
        if (wanted && !proven) { refused_ = true; return true; }
        const Kind kind = pending_;
        pending_ = Kind::None; refused_ = false;
        if (connect_(context_, kind)) return true; // connect_ marks window().connected at D+.
        abandon();
        return false;
    }

private:
    void abandon() { window_.abandon(); release(); }
    void release() {
        pending_ = Kind::None; refused_ = false;
        if (client_) client_->quietCapture(false);
    }
    EnumerationQuiet window_;
    Connect connect_;
    void *context_;
    QuietClient *client_ = nullptr;
    Kind pending_ = Kind::None;
    bool refused_ = false;
};
} // namespace openosc::usb
