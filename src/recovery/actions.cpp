#include "openosc/platform/watchdog.hpp"
#include "openosc/recovery/controller.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/handoff.hpp"
#include "openosc/platform/power.hpp"
#include <cstdio>

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
        views_.confirm("START APPLICATION?", update_.image().version, yes_); break;
    case Action::PowerHold: {
        char detail[24];
        std::snprintf(detail, sizeof detail, "%lu MS", static_cast<unsigned long>(holdValue_));
        views_.confirm("WRITE POWER-ON HOLD?", detail, yes_); break;
    }
    case Action::PowerOff:
        views_.confirm("POWER OFF?", "ALL DISK OPERATIONS ARE COMPLETE", yes_); break;
    case Action::CheckFat: views_.fatRepairConfirm(fatCheck_.report(), yes_); break;
    case Action::Format: views_.fatFormatConfirm(yes_); break;
    case Action::Calibration: case Action::Count: break;
    }
}
void RecoveryController::confirmKey(uint32_t key) {
    if ((key & Keypad::F4) || ((key & Keypad::Menu) && !yes_)) {
        if (action_ == Action::Update) { state_ = State::Files; views_.files(catalog_, file_); }
        else if (action_ == Action::PowerHold) { state_ = State::HoldEdit; views_.powerHold(holdCurrent_, holdValue_, holdDigit_); }
        else if (action_ == Action::CheckFat) { state_ = State::FatReport; views_.fatReport(fatCheck_.report()); }
        else showMain();
        return;
    }
    if (key & (Keypad::Left | Keypad::Right)) { yes_ = !yes_; drawConfirm(); }
    else if ((key & Keypad::Menu) && yes_) perform();
}
void RecoveryController::perform() {
    Watchdog::expect(LongOperationMs);
    if (action_ == Action::Update) {
        screen_.clear();
        bool success = update_.install(imagePath_);
        message(success ? "UPDATE COMPLETE" : "UPDATE FAILED",
                success ? "IMAGE VERIFIED. SELECT START APPLICATION." : update_.error());
    } else if (action_ == Action::Backup) {
        screen_.clear();
        bool success = backup_.create(overwrite_);
        if (success) { state_ = State::Message; views_.backupResult(backup_.crc()); }
        else message("BACKUP FAILED", backup_.error());
    } else if (action_ == Action::CheckFat) {
        repairFat();
    } else if (action_ == Action::Format) {
        formatStorage();
    } else if (action_ == Action::PowerHold) {
        writePowerHold();
    } else {
        if (!fs_.unmount() || !usb_.stop() || (storageReady_ && !nor_.restoreLegacyAddressing())) {
            message("STORAGE ERROR", "CANNOT FINISH DISK OPERATIONS");
            return;
        }
        if (action_ == Action::Launch) Handoff::jump(layout::VectorBase);
        if (action_ == Action::PowerOff) PowerControl::powerOff();
    }
}
void RecoveryController::beginPowerHold() {
    if (!PowerHold::read(holdCurrent_)) {
        message("POWER-ON HOLD", "FACTORY BOOTLOADER NOT SUPPORTED");
        return;
    }
    holdValue_ = holdCurrent_;
    holdDigit_ = 4;
    state_ = State::HoldEdit;
    views_.powerHold(holdCurrent_, holdValue_, holdDigit_);
}
void RecoveryController::powerHoldKey(uint32_t key) {
    if (key & Keypad::F4) { showMain(); return; }
    if (key & Keypad::Menu) {
        if (holdValue_ < PowerHold::MinMs || holdValue_ > PowerHold::MaxMs) {
            message("POWER-ON HOLD", "VALUE MUST BE 1 TO 65535 MS");
            return;
        }
        yes_ = false;
        state_ = State::Confirm;
        drawConfirm();
        return;
    }
    if (key & Keypad::Left) holdDigit_ = (holdDigit_ + 4) % 5;
    if (key & Keypad::Right) holdDigit_ = (holdDigit_ + 1) % 5;
    if (key & (Keypad::Up | Keypad::Down)) {
        uint32_t weight = 1;
        for (unsigned n = 4 - holdDigit_; n; --n) weight *= 10;
        const uint32_t digit = holdValue_ / weight % 10, next = (digit + ((key & Keypad::Up) ? 1 : 9)) % 10;
        holdValue_ = holdValue_ - digit * weight + next * weight;
    }
    views_.powerHold(holdCurrent_, holdValue_, holdDigit_);
}
void RecoveryController::writePowerHold() {
    if (holdValue_ == holdCurrent_) {
        message("POWER-ON HOLD UNCHANGED", "THE BOOTLOADER ALREADY HAS THIS VALUE");
        return;
    }
    if (!powerHold_.prepare(holdValue_)) {
        message("POWER-ON HOLD NOT SET", powerHold_.error());
        return;
    }
    commitPowerHold();
}
// From the erase on, the sector is not trusted until it verifies: a failure keeps the prepared copy.
void RecoveryController::commitPowerHold() {
    screen_.clear();
    if (powerHold_.write()) {
        char detail[24];
        std::snprintf(detail, sizeof detail, "%lu MS", static_cast<unsigned long>(holdValue_));
        message("POWER-ON HOLD SET", detail);
        return;
    }
    state_ = State::HoldFailed;
    views_.powerHoldFailed(powerHold_.error());
}
void RecoveryController::holdFailedKey(uint32_t key) {
    if (!(key & Keypad::Menu)) return;
    Watchdog::expect(LongOperationMs);
    commitPowerHold();
}
}
