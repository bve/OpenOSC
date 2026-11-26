#include "openosc/application/controller.hpp"
#include "openosc/usb/instrument_usb.hpp"

namespace openosc {
// Settings > Files & USB > USB controller (Preferences::usbController): Disabled, Enabled or Auto
// (GitHub #170). The one place it is applied: the boot, a settings reload and the Settings choice. A
// mode already applied is not applied again, so a reload that keeps Auto keeps its state. Entering
// Auto starts usb::AutoPolicy afresh: an attempt at the boot, or when the controller is already on
// (its link keeps the window); with it off, none until a PC9 rise or the first retry delay.
void ApplicationController::applyUsbController(uint32_t now) {
    if (!usb_ || prefs_.usbController == usbApplied_) return;
    const bool boot = usbApplied_ == 0xff;
    usbApplied_ = prefs_.usbController;
    bool on = usbApplied_ != Preferences::UsbDisabled;
    if (usbApplied_ == Preferences::UsbAuto) {
        on = boot || usb_->attached(); // On the bus or an attach pending.
        usbAuto_.begin(now, on, battery_.state().external, battery_.externalKnown());
    }
    usb_->enable(on, now);
}
// Auto's hardware adapter, once a pass after InstrumentUsb::poll: the policy's inputs from the device
// (the clock's permission, the actual attach, a host only by its SOFs: hostPresent), the battery's
// debounced PC9 and Remote; its answer through InstrumentUsb::enable, which does nothing while it is
// unchanged (no repeated attach requests) and leaves the attach to the quiet-capture and clock rules.
// Nothing during a shutdown or a Recovery handoff: those stop the device themselves.
void ApplicationController::serviceUsbAuto(uint32_t now) {
    if (!usb_ || usbApplied_ != Preferences::UsbAuto || shutdown_ != Shutdown::None || recoveryPending_) return;
    usb::AutoPolicy::Inputs in;
    in.now = now;
    in.external = battery_.state().external;
    in.externalKnown = battery_.externalKnown(); // The first debounce seeds; only later rises are edges.
    in.clock = usb_->clockAllows();
    in.onBus = usb_->onBus();
    in.host = usb_->hostPresent();
    in.remote = remote_;
    usb_->enable(usbAuto_.poll(in), now);
}
// Remote needs the link itself: a Host with a forbidden profile or an attach still waiting is off the bus.
bool ApplicationController::usbAutoOff() const { return usbApplied_ == Preferences::UsbAuto && !(usb_ && usb_->onBus()); }
} // namespace openosc
