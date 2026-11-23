#include "openosc/platform/watchdog.hpp"
#include "openosc/recovery/controller.hpp"
#include "openosc/storage/layout.hpp"
#include <cstdio>
#include "openosc/platform/board.hpp"
#include "openosc/platform/input.hpp"

namespace openosc {
[[noreturn]] void RecoveryController::run() {
    if (repairFailed_) message("FAT REPAIR FAILED", fatCheck_.error());
    else showMain();
    boot_diagnostics.stage = 4;
    while (true) {
        Watchdog::heartbeat(); // The one proof that Recovery's loop goes on (docs/WATCHDOG.md).
        Board::service();
        usb_.state();
        usb_.hostReleased();
        const uint32_t key = Keypad::pressed();
        if (key) onKey(key);
        serviceFormat();
        boot_diagnostics.action = uint32_t(state_) | (uint32_t(action_) << 8);
    }
}
void RecoveryController::message(const char* title, const char* detail) {
    state_ = State::Message;
    views_.message(title, detail);
}
void RecoveryController::showMain() {
    const char* status = nor_.error();
    if (storageReady_) {
        status = fs_.unmount() && usb_.start() ? "USB DISK AVAILABLE" : "USB DISK START FAILED";
    }
    state_ = State::Main;
    views_.menu(selected_, nor_.geometry().jedecId, nor_.geometry().capacity,
                status ? status : "STORAGE NOT AVAILABLE");
}
bool RecoveryController::releaseHost() {
    if (!usb_.hostReleased()) {
        state_ = State::Disconnect;
        yes_ = false;
        views_.disconnect(yes_);
        return false;
    }
    if (!usb_.stop()) {
        message("USB ERROR", "CANNOT RELEASE THE DISK");
        return false;
    }
    return true;
}
void RecoveryController::begin(Action action) {
    action_ = action;
    if (action == Action::Calibration) {
        state_ = State::Calibration;
        views_.calibration(channel_, hex_);
        return;
    }
    if (!releaseHost()) return;
    if (action == Action::CheckFat) { checkFat(); return; } // The explicit check reports everything.
    if (action == Action::PowerHold) { beginPowerHold(); return; }
    // The host session just ended: its dirty flag and the factory label, before FatFs mounts.
    if (storageReady_ && action != Action::Format && !fatCheck_.reconcile()) {
        message("FAT REPAIR FAILED", fatCheck_.error());
        return;
    }
    if (action == Action::Update || action == Action::Backup) {
        if (!storageReady_ || !fs_.mountPrepared()) {
            message("FAT ERROR", "FAT VOLUME OR DIRECTORIES NOT READY");
            return;
        }
    }
    if (action == Action::Update) {
        if (!catalog_.load(0)) {
            char detail[40];
            std::snprintf(detail, sizeof detail, "CANNOT READ %s", storage::Layout::Update);
            message("FAT ERROR", detail); return;
        }
        file_ = 0;
        state_ = State::Files;
        views_.files(catalog_, file_);
    } else {
        if (action == Action::Launch && !update_.installed(true)) {
            message("CANNOT START APPLICATION", update_.error());
            return;
        }
        overwrite_ = action == Action::Backup && backup_.exists();
        yes_ = false;
        state_ = State::Confirm;
        drawConfirm();
    }
}
void RecoveryController::onKey(uint32_t key) {
    switch (state_) {
    case State::Main:
        if (key & Keypad::Up) selected_ = (selected_ + RecoveryItemCount - 1) % RecoveryItemCount;
        if (key & Keypad::Down) selected_ = (selected_ + 1) % RecoveryItemCount;
        if (key & Keypad::Menu) begin(static_cast<Action>(selected_));
        else if (key & (Keypad::Up | Keypad::Down)) showMain();
        break;
    case State::Disconnect: disconnectKey(key); break;
    case State::FatReport: fatReportKey(key); break;
    case State::Formatting: break; // The confirmed metadata rewrite must finish before other actions.
    case State::Files: filesKey(key); break;
    case State::Confirm: confirmKey(key); break;
    case State::HoldEdit: powerHoldKey(key); break;
    case State::HoldFailed: holdFailedKey(key); break; // Only a verified write leaves this state.
    case State::Message:
        if (key & (Keypad::Menu | Keypad::F4)) showMain();
        break;
    case State::Calibration:
        if (key & Keypad::F4) { showMain(); break; }
        if (key & (Keypad::Left | Keypad::Right)) channel_ ^= 1;
        if (key & (Keypad::Menu | Keypad::Up | Keypad::Down)) hex_ = !hex_;
        views_.calibration(channel_, hex_);
        break;
    }
}
}
