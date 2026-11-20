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

// When the capture must be quiet for a host's enumeration: one bounded window per host episode, and
// only for a host proven on the bus. Full-rate capture DMA during enumeration coincided with failed
// CDC enumeration (descriptor -110) and a host controller that gave up (HIL 2026-11-15, Normal on DC
// at 200/100). Without a host nothing waits (GitHub #35: a boot on battery, each return to the Scope
// and a pulled cable each held the capture about 5 s for a host that was not there).
// - request(): a new logical attach is asked for (a planned reattach, a start after a full stop; the
//   boot is one). attach() then arms it once: the next host's first proven reset opens the window.
//   The attach itself waits for a quiet capture apart from the window (QuietAttach).
// - reset(now): a bus reset with a host proven by its SOFs (InstrumentUsb::poll). It opens the window
//   once per episode, never extends or reopens it: a retry (the address watchdog's) adds no budget.
// - quiet(now, host): the window holds while `host` (HostPresence::present: its grace covers a bus
//   reset's SOF gap) and for at most BudgetMs from its opening. A host gone (pulled cable, suspend)
//   ends it at once and leaves one window for the next host; a host that keeps failing ends it at the
//   budget and gets no more (no periodic pauses).
// - configured(): a healthy host ends the window at once; a later host (unplugged, plugged again)
//   has one window of its own at its first reset.
// - abandon(): the request failed (initialization or connect); stopped(): a full stop, which allows
//   the next logical attach.
// The budget is a deadline from the window's start: a `now` from before it (a poll's tick taken
// before the window opened) never ends it. Modular milliseconds.
class EnumerationQuiet {
public:
    static constexpr uint32_t BudgetMs = 5000;
    void request() { armed_ = true; }
    void attach() {
        if (!armed_) return;
        armed_ = false; awaitingHost_ = true;
    }
    void abandon() { active_ = false; awaitingHost_ = false; armed_ = false; }
    void reset(uint32_t now) {
        if (active_ || !awaitingHost_) return;
        awaitingHost_ = false; active_ = true; startedAt_ = now;
    }
    void configured() { active_ = false; awaitingHost_ = true; }
    void stopped() { active_ = false; awaitingHost_ = false; armed_ = true; }
    bool quiet(uint32_t now, bool host) {
        if (!active_) return false;
        if (!host) { active_ = false; awaitingHost_ = true; }
        else if (int32_t(now - startedAt_) >= int32_t(BudgetMs)) active_ = false;
        return active_;
    }
    bool active() const { return active_; } // As the last quiet() left it.

private:
    uint32_t startedAt_ = 0;
    bool active_ = false, awaitingHost_ = false, armed_ = true; // The boot is a request.
};

// Every attach of the device to the bus goes through here: the start (USBD_Start after a full
// initialization) and the reconnect (the address watchdog's DevDisconnect/DevConnect). What is
// wanted waits until the capture is proven quiet, and keeps waiting until it is (a stopped capture's
// proof, a pending HOLD's end): an attach never follows an unproven stop, host or not. Then `connect`
// runs once for it (a start supersedes a reconnect). service() runs each poll: it tells the capture
// to be quiet while an attach waits or the window holds, else not, so captures resume at the next
// poll after an attach and as soon as the window ends. A start whose initialization or connect fails
// abandons its request: the capture is released at once.
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
    // A logical request (InstrumentUsb::reattach): the next start arms a window for its host.
    void request() { window_.request(); }
    // InstrumentUsb::start: `initialized` the USBD set-up's result. True when attached or waiting.
    bool start(uint32_t now, bool initialized, bool host) {
        if (!initialized) { abandon(); return false; }
        window_.attach(); // Arms only for a new request: a retried start does not.
        want(Kind::Start);
        return service(now, host);
    }
    // `host`: HostPresence::present now. False only when a connect ran and failed (then abandoned).
    bool service(uint32_t now, bool host) {
        const bool wanted = window_.quiet(now, host) || pending();
        const bool proven = !client_ || client_->quietCapture(wanted);
        if (!pending() || !proven) return true; // Nothing to attach, or it waits for the proof.
        const Kind kind = pending_;
        pending_ = Kind::None;
        if (connect_(context_, kind)) return true;
        abandon();
        return false;
    }
    bool quiet() const { return window_.active(); } // The window holds for an enumerating host.

private:
    void abandon() { window_.abandon(); release(); }
    void release() {
        pending_ = Kind::None;
        if (client_) client_->quietCapture(false);
    }
    EnumerationQuiet window_;
    Connect connect_;
    void *context_;
    QuietClient *client_ = nullptr;
    Kind pending_ = Kind::None;
};
} // namespace openosc::usb
