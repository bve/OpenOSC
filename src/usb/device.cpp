#include "openosc/usb/usb_device.hpp"
#include "openosc/platform/board.hpp"
#include "usbd_msc_scsi.h"
#include "usbd_msc_bot.h"

namespace openosc {
USBD_HandleTypeDef usbDevice{};
PCD_HandleTypeDef usbPcd{};
volatile bool usbHostClaimed = false;
volatile uint32_t usbLastActivity = 0;

bool UsbMassStorage::start() {
    if (active_) return true;
    usbHostClaimed = false;
    usbLastActivity = Board::milliseconds();
    ejectedSince_ = 0;
    if (!disk_.setOwner(MediaOwner::Host)) return false; // The cached block first.
    setUsbStorage(&disk_);
    if (USBD_Init(&usbDevice, &recoveryDescriptors, 0) != USBD_OK
        || USBD_RegisterClass(&usbDevice, &USBD_MSC) != USBD_OK
        || USBD_MSC_RegisterStorage(&usbDevice, &recoveryStorage) != USBD_OK
        || USBD_Start(&usbDevice) != USBD_OK) {
        USBD_DeInit(&usbDevice);
        disk_.setOwner(MediaOwner::Local);
        return false;
    }
    active_ = true;
    return true;
}
bool UsbMassStorage::stop() {
    if (!active_) return true;
    const uint32_t start = Board::milliseconds();
    while (true) {
        // Quiesce a complete BOT command before detaching. IRQ masking makes
        // the final idle check and disconnect indivisible to new USB traffic.
        HAL_NVIC_DisableIRQ(OTG_HS_IRQn);
        const uint32_t now = Board::milliseconds();
        auto* msc = static_cast<USBD_MSC_BOT_HandleTypeDef*>(usbDevice.pClassDataCmsit[0]);
        const bool idle = !msc || msc->bot_state == USBD_BOT_IDLE;
        if ((idle && now - usbLastActivity >= 20)
            || (now - start >= 1000 && usbDevice.dev_state != USBD_STATE_CONFIGURED)) break;
        HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
        if (now - start >= 1000) return false;
        Board::service();
    }
    if (USBD_DeInit(&usbDevice) != USBD_OK) {
        HAL_NVIC_EnableIRQ(OTG_HS_IRQn);
        return false;
    }
    HAL_NVIC_ClearPendingIRQ(OTG_HS_IRQn);
    active_ = false;
    usbHostClaimed = false;
    disk_.setOwner(MediaOwner::Local);
    Board::delay(30);
    return disk_.sync();
}
bool UsbMassStorage::hostReleased() {
    if (!active_ || !usbHostClaimed) return true;
    auto* state = static_cast<USBD_MSC_BOT_HandleTypeDef*>(usbDevice.pClassDataCmsit[0]);
    if (!state || state->scsi_medium_state != SCSI_MEDIUM_EJECTED) { ejectedSince_ = 0; return false; }
    uint32_t now = Board::milliseconds();
    if (!ejectedSince_) ejectedSince_ = now ? now : 1;
    // Let the eject command's CSW reach the host before disconnecting USB.
    return now - ejectedSince_ >= 250;
}
uint32_t UsbMassStorage::state() const {
    const uint32_t result = active_ ? usbDevice.dev_state : 0;
    boot_diagnostics.usbState = result;
    return result;
}
}

