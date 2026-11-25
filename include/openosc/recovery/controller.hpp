#pragma once
#include "openosc/recovery/screens.hpp"
#include "openosc/recovery/backup.hpp"
#include "openosc/recovery/action.hpp"
#include "openosc/storage/fat/formatter.hpp"
#include "openosc/recovery/fat_check.hpp"
#include "openosc/storage/nor_flash.hpp"
#include "openosc/update/update_operation.hpp"
#include "openosc/usb/usb_device.hpp"

namespace openosc {
class RecoveryController {
public:
    // What Recovery's long operations may take in one pass (an update, a backup, a FAT check of the
    // whole NOR); their progress reports keep the watchdog fed only within it (WatchdogPolicy).
    static constexpr uint32_t LongOperationMs = 300000;
    RecoveryController(Display& screen, NorFlash& nor, Filesystem& fs,
                       UsbMassStorage& usb, UpdateOperation& update, FatCheckOperation& fatCheck, BlockDevice& disk, bool storageReady)
        : screen_(screen), views_(screen), nor_(nor), fs_(fs), usb_(usb),
          update_(update), fatCheck_(fatCheck), formatter_(disk,fs),backup_(screen, fs), storageReady_(storageReady) {}
    [[noreturn]] void run();
    void reportFatRepairFailure() { repairFailed_ = true; } // Shown instead of the main menu once.
private:
    enum class State { Main, Disconnect, Files, Calibration, Confirm, Message, FatReport, Formatting };
    using Action = RecoveryAction;
    void onKey(uint32_t key);
    void begin(Action action);
    void perform();
    void filesKey(uint32_t key);
    void confirmKey(uint32_t key);
    void disconnectKey(uint32_t key);
    void checkFat();
    void repairFat();
    void fatReportKey(uint32_t key);
    void formatStorage();
    void serviceFormat();
    void drawConfirm();
    void showMain();
    void message(const char* title, const char* detail);
    bool releaseHost();
    Display& screen_;
    RecoveryScreens views_;
    NorFlash& nor_;
    Filesystem& fs_;
    UsbMassStorage& usb_;
    UpdateOperation& update_;
    FatCheckOperation& fatCheck_;
    fat::Formatter formatter_;
    FactoryBackup backup_;
    ImageCatalog catalog_;
    const bool storageReady_;
    bool repairFailed_ = false;
    State state_ = State::Main;
    Action action_ = Action::Update;
    unsigned selected_ = 0, file_ = 0, channel_ = 0;
    bool yes_ = false, hex_ = true, overwrite_ = false;
    bool updated_ = false; // The start offer follows the update just installed.
    char imagePath_[FF_MAX_LFN + 10]{};
};
}
