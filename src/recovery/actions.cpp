#include "openosc/platform/watchdog.hpp"
#include "openosc/recovery/controller.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/handoff.hpp"
#include "openosc/platform/power.hpp"

namespace openosc {
void RecoveryController::filesKey(uint32_t key) {
    if (key & Keypad::F4) { showMain(); return; }
    unsigned count = catalog_.count();
    if (count && (key & Keypad::Up)) file_ = (file_ + count - 1) % count;
    if (count && (key & Keypad::Down)) file_ = (file_ + 1) % count;
    unsigned page = catalog_.page();
    if ((key & Keypad::Left) && page) --page;
    if ((key & Keypad::Right) && (page + 1) * ImageCatalog::PageSize < catalog_.total()) ++page;
    if (page != catalog_.page()) {
        if (!catalog_.load(page)) { message("FAT ERROR", "CANNOT READ IMAGE DIRECTORY"); return; }
        file_ = 0;
    }
    if ((key & Keypad::Menu) && count) {
        if (!catalog_.path(file_, imagePath_, sizeof imagePath_) || !update_.inspect(imagePath_)) {
            message("IMAGE REJECTED", update_.error() ? update_.error() : "IMAGE PATH TOO LONG");
            return;
        }
        yes_ = false;
        state_ = State::Confirm;
        drawConfirm();
    } else views_.files(catalog_, file_);
}
void RecoveryController::drawConfirm() {
    switch (action_) {
    case Action::Update:
        views_.confirm("INSTALL FIRMWARE?", catalog_.name(file_), yes_, &update_.image(), update_.rawBinary()); break;
    case Action::Backup:
        views_.confirm(overwrite_ ? "REPLACE EXISTING FACTORY BACKUP?" : "CREATE FACTORY BACKUP?",
                       storage::Layout::FactoryBackup, yes_); break;
    case Action::Launch:
        views_.confirm(updated_ ? "UPDATE COMPLETE. START APPLICATION?" : "START APPLICATION?", update_.image().version, yes_); break;
    case Action::PowerOff:
        views_.confirm("POWER OFF?", "ALL DISK OPERATIONS ARE COMPLETE", yes_); break;
    case Action::CheckFat: views_.fatRepairConfirm(fatCheck_.report(), yes_); break;
    case Action::Format: views_.fatFormatConfirm(yes_); break;
    case Action::Calibration: case Action::Count: break;
    }
}
// F2 and F3 are CANCEL and CONFIRM at once, without choosing with LEFT/RIGHT first.
void RecoveryController::confirmKey(uint32_t key) {
    if ((key & (Keypad::F2 | Keypad::F4)) || ((key & Keypad::Menu) && !yes_)) {
        if (action_ == Action::Update) { state_ = State::Files; views_.files(catalog_, file_); }
        else if (action_ == Action::CheckFat) { state_ = State::FatReport; views_.fatReport(fatCheck_.report()); }
        else showMain();
        return;
    }
    if ((key & Keypad::F3) || ((key & Keypad::Menu) && yes_)) perform();
    else if (key & (Keypad::Left | Keypad::Right)) { yes_ = !yes_; drawConfirm(); }
}
void RecoveryController::perform() {
    Watchdog::expect(LongOperationMs);
    if (action_ == Action::Update) {
        screen_.clear();
        if (!update_.install(imagePath_)) { message("UPDATE FAILED", update_.error()); return; }
        // START APPLICATION offered at once, on its own check of the installed image, CONFIRM preselected.
        if (!update_.installed(true)) { message("CANNOT START APPLICATION", update_.error()); return; }
        action_ = Action::Launch;
        updated_ = yes_ = true;
        state_ = State::Confirm;
        drawConfirm();
    } else if (action_ == Action::Backup) {
        screen_.clear();
        bool success = backup_.create(overwrite_);
        if (success) { state_ = State::Message; views_.backupResult(backup_.crc()); }
        else message("BACKUP FAILED", backup_.error());
    } else if (action_ == Action::CheckFat) {
        repairFat();
    } else if (action_ == Action::Format) {
        formatStorage();
    } else {
        if (!fs_.unmount() || !usb_.stop() || (storageReady_ && !nor_.restoreLegacyAddressing())) {
            message("STORAGE ERROR", "CANNOT FINISH DISK OPERATIONS");
            return;
        }
        if (action_ == Action::Launch) Handoff::jump(layout::VectorBase);
        if (action_ == Action::PowerOff) PowerControl::powerOff();
    }
}
}
