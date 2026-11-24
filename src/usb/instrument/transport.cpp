#include "openosc/usb/instrument_usb.hpp"
#include "openosc/usb/instrument_class.hpp"
#include "openosc/usb/usb_device.hpp"
#include "openosc/platform/critical_section.hpp"

namespace openosc {
namespace {
// The OTG controller for USBTMC split transactions: the global OUT NAK (RM0455 section
// 62.15.6, "Setting the global OUT NAK"), the TMC OUT endpoint's pending transfer-complete and
// whether the TMC IN endpoint still holds a transfer; register accesses only, never a wait.
class ControllerPort final : public usb::TmcRecovery::Port {
public:
    void request(bool on) override {
        device()->DCTL |= on ? USB_OTG_DCTL_SGONAK : USB_OTG_DCTL_CGONAK;
    }
    bool effective() const override { return usbPcd.Instance->GINTSTS & USB_OTG_GINTSTS_BOUTNAKEFF; }
    bool completionPending() const override { return out1()->DOEPINT & USB_OTG_DOEPINT_XFRC; }
    bool bulkInBusy() const override { return in1()->DIEPCTL & USB_OTG_DIEPCTL_EPENA; }
private:
    static uintptr_t base() { return reinterpret_cast<uintptr_t>(usbPcd.Instance); }
    static USB_OTG_DeviceTypeDef *device() { return reinterpret_cast<USB_OTG_DeviceTypeDef*>(base() + USB_OTG_DEVICE_BASE); }
    static USB_OTG_INEndpointTypeDef *in1() {
        return reinterpret_cast<USB_OTG_INEndpointTypeDef*>(base() + USB_OTG_IN_ENDPOINT_BASE + USB_OTG_EP_REG_SIZE);
    }
    static USB_OTG_OUTEndpointTypeDef *out1() {
        return reinterpret_cast<USB_OTG_OUTEndpointTypeDef*>(base() + USB_OTG_OUT_ENDPOINT_BASE + USB_OTG_EP_REG_SIZE);
    }
};
ControllerPort controllerPort;
}
void InstrumentUsb::releaseOutNak() { usb::tmcRecovery.release(controllerPort); }
void InstrumentUsb::serviceTmc(uint32_t now) { tmcPipe_.dispatch(now, generation_, controllerPort); }
void InstrumentUsb::synchronize() {
    unsigned mask;
    {
        CriticalSection lock;
        mask = usb::instrument.clearMask; usb::instrument.clearMask = 0;
        if (generation_ != usb::instrument.generation) { generation_ = usb::instrument.generation; mask = 3; }
    }
    if (mask & 1) tmc_.reset();
    if (mask & 2) { serialEngine_.reset(); serialZlp_ = false; }
}
void InstrumentUsb::transfer(uint32_t now) {
    auto &serialPipe = usb::instrument.pipes[1];
    // Only into this session's engine: not after a reset, configuration or DTR change until
    // synchronize() has reset it (usb::consume).
    usb::consume(1, generation_, [] { return true; }, [&](uint8_t byte) { return serialEngine_.feed(byte, now); });
    if (!serialPipe.transmitBusy) {
        if (serialEngine_.available()) {
            const auto count = serialEngine_.read(serialPipe.transmit,usb::TransferBytes);
            if (count && usb::transmit(1,count,generation_)) serialZlp_ = !(count & 63);
        } else if (serialZlp_ && usb::transmit(1,0,generation_)) serialZlp_ = false;
    }
}
void InstrumentUsb::background(uint32_t now) {
    if (!active_ || polling_) return;
    Polling guard(polling_);
    synchronize();
    if (!connected()) return;
    // Queue input and send already prepared output. Instrument commands are
    // dispatched by the main poll only, never recursively inside a FAT write.
    tmcPipe_.service(now, generation_, controllerPort);
    transfer(now);
}
} // namespace openosc
