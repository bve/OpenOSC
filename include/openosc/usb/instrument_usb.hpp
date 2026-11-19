#pragma once
#include "openosc/scpi/tmc.hpp"
#include "openosc/usb/address_watchdog.hpp"
#include "openosc/usb/clock_policy.hpp"
#include "openosc/usb/enumeration_quiet.hpp"
#include "openosc/usb/host_presence.hpp"
#include "openosc/usb/reattach_plan.hpp"
#include "openosc/usb/tmc_pipe.hpp"
#include "usbd_core.h"

namespace openosc {
class InstrumentUsb {
public:
    InstrumentUsb(scpi::Engine &tmc, scpi::Engine &serial) : tmcEngine_(tmc), serialEngine_(serial), tmc_(tmc) {}
    InstrumentUsb(const InstrumentUsb &) = delete;
    InstrumentUsb &operator=(const InstrumentUsb &) = delete;
    // The data link runs where both allow it: the clock that runs (ClockPolicy, followClock) and the
    // user's choice (Settings > Files & USB > USB controller, enable). Where they do not, the device
    // is stopped (stop(): the OTG core reset, its clock and interrupt off) and start()/reattach()
    // refuse, so neither a planned attach nor the address watchdog brings it back until it is allowed
    // again. Where they do, the device stays attached with or without a host (a later host
    // enumerates it; HostPresence). The two are kept apart: a run-mode change never alters the choice.
    void followClock(uint32_t activeMode) {
        clocked_ = usb::ClockPolicy::allows(activeMode);
        if (!allowed()) stop();
    }
    // The user's choice (Preferences::usbController), applied at once. Off stops the device; on, where
    // the clock allows it, asks for one planned attach (reattach). Unchanged: nothing.
    void enable(bool on, uint32_t now) {
        if (on == enabled_) return;
        enabled_ = on;
        if (!on) stop();
        else reattach(now);
    }
    bool enabled() const { return enabled_; }
    bool allowed() const { return enabled_ && clocked_; }
    // The capture that must be quiet while a host enumerates (QuietAttach); set before start().
    void quietWith(usb::QuietClient *client) { attach_.client(client); }
    // Initializes the device (USBD) and asks for its attach. True when it is attached or its attach
    // is accepted and waits for the capture to be quiet (poll attaches it then); false when not
    // allowed, when the initialization failed or when an attach that ran failed. Called again while
    // its attach waits, it neither initializes again nor attaches twice.
    bool start();
    // Full de-initialization (D+ released; the OTG core reset, its clock and interrupt off). Also
    // from a command that poll() dispatches: the class de-initialization advances the session
    // generation, so the rest of that pass neither feeds, dispatches nor transmits for it
    // (usb::current), and synchronize() resets both engines again before the next session's data.
    void stop();
    void poll(uint32_t now);
    // Attached again by poll(), through start() (full initialization, RCC reset of the OTG core),
    // ReattachPlan::DetachMs after `now`.
    void reattach(uint32_t now) {
        if (!allowed()) return;
        reattach_.request(now); busActivityAt_ = now;
        attach_.request(now); // A new logical attach: quiet from the request, before the attach DetachMs later.
    }
    void profileChanged(uint32_t now) { stop(); reattach(now); }
    bool attached() const { return active_ || reattach_.pending() || attach_.pending(); } // Or an attach pending.
    bool connected() const;
    // True after attaching and while a host keeps resetting the bus without finishing enumeration.
    bool enumerating(uint32_t now) const {
        // No host to wait for: no bus reset FirstResetMs after the attach, or no SOFs NoHostMs after it.
        const uint32_t window = host_.seen() ? EnumerationMs : resetSinceAttach_ ? NoHostMs : usb::EnumerationQuiet::FirstResetMs;
        return active_ && !connected() && (int32_t(now-attachedAt_) < int32_t(window) ||
                                           (host_.seen() && int32_t(now-busActivityAt_) < int32_t(HostQuietMs)));
    }
    // True while a host is enumerating or using the device: power-hungry bursts wait. Bus resets
    // count only while a host is proven by its SOFs (poll).
    bool hostActive(uint32_t now) const;
private:
    static constexpr uint32_t HostQuietMs = 5000;
    static constexpr uint32_t EnumerationMs = 3000, NoHostMs = 1000; // SOFs after an unproven reset (a charger's lines).
    // A host unproven this long is gone (pulled cable, suspended PC): the watchdog's episode ends with
    // it, and a later host starts a new one. Longer than HostPresence::GraceMs (a bus reset).
    static constexpr uint32_t HostLostMs = 1000;
    uint32_t attachedAt_ = 0, attachResets_ = 0; // usbResets at the attach: a reset since is a host's.
    bool resetSinceAttach_ = false;              // poll(): usbResets moved since attachResets_.
    uint32_t busActivityAt_ = 0, busResets_ = 0;
    struct Polling {
        explicit Polling(bool &flag):flag_(flag) { flag_=true; }
        ~Polling() { flag_=false; }
        bool &flag_;
    };
    void background(uint32_t now);
    void synchronize();
    void transfer(uint32_t now); // The CDC byte stream.
    void serviceTmc(uint32_t now, bool dispatch);
    void releaseOutNak();
    void release(bool setUp); // USBD_DeInit when anything was set up, then UsbCore::powerOff.
    static bool connect(void *context, usb::QuietAttach::Kind kind); // QuietAttach's one way onto the bus.
    scpi::Engine &tmcEngine_, &serialEngine_;
    scpi::Tmc tmc_;
    usb::TmcPipe tmcPipe_{tmc_, tmcEngine_, usb::tmcRecovery};
    usb::AddressWatchdog watchdog_;
    usb::ReattachPlan reattach_;
    usb::QuietAttach attach_{&InstrumentUsb::connect, this};
    usb::HostPresence host_;
    uint32_t generation_ = 0;
    bool active_ = false, polling_ = false, initialized_ = false; // initialized_: USBD set up, not started yet.
    bool clocked_ = false, enabled_ = true; // followClock, enable (the default: Enabled).
    bool hostPresent_ = false; // HostPresence at the last poll.
    bool serialZlp_ = false;
};
} // namespace openosc
