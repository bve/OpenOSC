#include "openosc/usb/instrument_usb.hpp"
#include "openosc/usb/instrument_class.hpp"
#include "openosc/usb/usb_device.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/critical_section.hpp"
#include "openosc/platform/dma_memory.hpp"
#include "openosc/platform/cooperative_service.hpp"
#include "openosc/platform/usb_core.hpp"
#ifdef OPENOSC_USB_TRACE
#include "openosc/platform/usb_trace.hpp"
#endif

namespace openosc {
USBD_HandleTypeDef usbDevice OPENOSC_DMA_DATA {};
PCD_HandleTypeDef usbPcd OPENOSC_DMA_DATA {};
volatile bool usbHostClaimed = false;
volatile uint32_t usbLastActivity = 0;
volatile uint32_t usbResets = 0;
bool InstrumentUsb::start() {
    if (!allowed()) return false;
    if (active_) return true;
    if (!initialized_) {
        initialized_ = USBD_Init(&usbDevice,&usb::instrumentDescriptors,0) == USBD_OK &&
                       USBD_RegisterClass(&usbDevice,&usb::instrumentClass) == USBD_OK;
        if (!initialized_) release(true); // A partial set-up may have enabled the OTG line (MspInit).
    }
    // A failed set-up or connect abandons this request: the capture is released at once.
    return attach_.start(Board::milliseconds(),initialized_);
}
bool InstrumentUsb::connect(void *context,usb::QuietAttach::Kind kind) {
    auto &self=*static_cast<InstrumentUsb*>(context);
    if (kind==usb::QuietAttach::Kind::Reconnect) {
        if (!self.active_) return false;
        // HAL_PCD_DevDisconnect drives D+ low; the host then enumerates afresh.
#ifdef OPENOSC_USB_TRACE
        usbtrace::mark(usbtrace::Tag::Reattach, usbResets);
#endif
        self.attachResets_=usbResets; self.resetSinceAttach_=false; // Before the detach: a reset during it is this attach's host.
        HAL_PCD_DevDisconnect(&usbPcd);
        Board::delay(10);
        HAL_PCD_DevConnect(&usbPcd); self.attachedAt_=Board::milliseconds(); self.host_.mark();
        self.attach_.window().connected(self.attachedAt_);
        return true;
    }
    if (self.active_ || !self.initialized_) return self.active_;
    // Factory/Recovery may leave a connected core while validating the application. Give the
    // host a real detach interval before presenting a different USB function, unless stop()
    // already detached long enough (ReattachPlan).
    if (self.reattach_.needsDetach(Board::milliseconds())) {
        USBD_LL_Stop(&usbDevice);
        Board::delay(usb::ReattachPlan::DetachMs);
    }
    if (USBD_Start(&usbDevice) != USBD_OK) { self.release(true); self.initialized_=false; return false; }
    self.active_=true; self.initialized_=false; self.attachedAt_=Board::milliseconds(); self.reattach_.started();
    self.attachResets_=usbResets; self.resetSinceAttach_=false; self.attach_.window().connected(self.attachedAt_);
    self.watchdog_=usb::AddressWatchdog{}; // A new session: no host, deadline or retry of the last one.
    self.host_=usb::HostPresence{}; self.hostPresent_=false; self.busResets_=usbResets;
    CooperativeService::install([](void *context,uint32_t now) {
        static_cast<InstrumentUsb*>(context)->background(now);
    },&self);
    return true;
}
// The one teardown of every path (stop, failed set-up, failed connect). USBD_DeInit ->
// HAL_PCD_DeInit -> HAL_PCD_Stop masks the core's interrupts and releases D+ (DCTL.SDIS); the weak
// HAL_PCD_MspDeInit leaves the core clocked and its line enabled in the NVIC, so UsbCore::powerOff
// then resets the core (PHY off), stops its clock and disables the line, together.
// HAL_PCD_MspInit clocks, resets and enables it at the next initialization.
void InstrumentUsb::release(bool setUp) {
    CriticalSection lock;
    if (setUp) USBD_DeInit(&usbDevice);
    UsbCore::powerOff();
}
void InstrumentUsb::stop() {
    CooperativeService::remove(this);
    releaseOutNak(); // Not left set when the controller stops.
    reattach_.cancel(); // Also when already stopped: a pending planned attach does not outlive a stop.
    attach_.stop();     // Nor an attach waiting for a quiet capture; the capture may run.
    watchdog_=usb::AddressWatchdog{};
    host_=usb::HostPresence{}; hostPresent_=false;
    release(active_ || initialized_); // initialized_: set up, never on the bus.
    if (active_) { active_ = false; reattach_.stopped(Board::milliseconds()); }
    initialized_ = false;
    tmc_.reset(); serialEngine_.reset(); serialZlp_ = false;
}
bool InstrumentUsb::connected() const {
    return active_ && usb::instrument.configured && usbDevice.dev_state == USBD_STATE_CONFIGURED;
}
bool InstrumentUsb::hostActive(uint32_t now) const {
    return connected() || hostPresent_ || int32_t(now-busActivityAt_) < int32_t(HostQuietMs);
}
void InstrumentUsb::poll(uint32_t now) {
    const auto *registers = reinterpret_cast<const USB_OTG_DeviceTypeDef*>(
        reinterpret_cast<uintptr_t>(usbPcd.Instance) + USB_OTG_DEVICE_BASE);
    // The SOF stream at the tick of its read (the loop's `now` may be older than the register).
    const uint32_t at = Board::milliseconds();
    if (active_) host_.sample(at, (registers->DSTS & USB_OTG_DSTS_FNSOF) >> USB_OTG_DSTS_FNSOF_Pos);
    hostPresent_ = active_ && host_.present(at);
    const bool reset = active_ && usbResets != attachResets_;
    if (reset && !resetSinceAttach_) attach_.window().answered(); // The first reset after this attach.
    resetSinceAttach_ = reset;
    // A bus reset is a host's activity (quiet window, saves wait) only once its SOFs prove it there;
    // until then it waits, so a host plugged in later still gets its window and resets without a
    // proven host give none.
    if (hostPresent_ && usbResets != busResets_) { busResets_ = usbResets; busActivityAt_ = now; attach_.window().reset(now); }
    if (polling_) return;
    Polling guard(polling_);
    if (connected()) attach_.configured(); // A healthy host: the capture runs again at once, no stale reconnect.
    if (reattach_.due(now)) start(); // start() services the attach itself (and abandons a failed one).
    else attach_.service(now);       // The window is a deadline: this pass's earlier `now` never ends it.
    if (!active_) return;
    // The watchdog's reconnects, and its hostSeen_, only for a host proven by its SOFs (a host that
    // lost the device's address keeps sending them). Without one - none plugged in, a pulled cable,
    // a suspended PC - the device stays passively attached: no periodic D+ toggling, and a host's own
    // reset and enumeration (or resume) take it as usual. A host gone HostLostMs ends the episode.
    if (!hostPresent_) {
        if (host_.absentFor(at) >= HostLostMs) watchdog_ = usb::AddressWatchdog{};
    } else {
        // A suspended host still owns the configured device, so it must not be reattached.
        const bool enumerated=connected() || (usb::instrument.configured && usbDevice.dev_state==USBD_STATE_SUSPENDED &&
                                              usbDevice.dev_old_state==USBD_STATE_CONFIGURED);
        if (watchdog_.update(now,usbResets,boot_diagnostics.usbReads,enumerated && (registers->DCFG & USB_OTG_DCFG_DAD))) {
            attach_.window().reset(now); // A retry of the same episode: no new budget.
            attach_.want(usb::QuietAttach::Kind::Reconnect);
            attach_.service(Board::milliseconds()); // Now if the capture is quiet, else from a later poll.
        }
    }
    synchronize();
    if (!connected()) return;
    serialEngine_.poll(now);
    serviceTmc(now, true); // USBTMC: split transactions first, then the bulk pipe.
    transfer(now);
    boot_diagnostics.usbState = usbDevice.dev_state;
}
}
extern "C" void *recovery_usb_allocate(uint32_t) { return nullptr; }
extern "C" void recovery_usb_free(void *) {}
extern "C" void recovery_usb_delay(uint32_t ms) { openosc::Board::delay(ms); }
