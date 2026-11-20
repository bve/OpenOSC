#include "openosc/platform/watchdog.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/handoff.hpp"
#include "openosc/platform/recovery_request.hpp"
#include "openosc/platform/reset_record.hpp"
#include "openosc/recovery/controller.hpp"
#include "openosc/update/crc32.hpp"

int main() {
    using namespace openosc;
    Display::takeBootFrame(); // Discard a marker left by an older app that did not consume it.
    if (!Board::initialize()) Watchdog::fatal(boot_diagnostics.error);
    Board::delay(30);
    const bool requestedByApp = RecoveryRequest::take();
    // Recovery itself faulted last time: its menu, not an auto-start into the same fault (taken once).
    const bool faulted = ResetRecord::takeRecoveryFault(ResetRecord::retained());
    const bool recoveryRequested = (Keypad::read() & Keypad::F2) || requestedByApp || faulted;
    Display screen;
    UpdateOperation update(screen);
    bool logoShown = false;
#ifndef OPENOSC_RAM_TEST
    // Show the logo while CRC checks run. A damaged application still falls back to the menu.
    if (!recoveryRequested) logoShown = screen.initialize(true);
#endif
    boot_diagnostics.factoryCrc = Crc32::compute(reinterpret_cast<const void*>(layout::FactoryBase), layout::FactoryBytes);
#ifndef OPENOSC_RAM_TEST
    if (!recoveryRequested && update.installed()) {
        if (logoShown) screen.offerBootFrame();
        Handoff::jump(layout::VectorBase);
    }
#else
    (void)recoveryRequested;
#endif
    if (!logoShown && !screen.initialize(true)) {
        boot_diagnostics.error = 2;
        Watchdog::fatal(2);
    }
    boot_diagnostics.stage = 3;
    screen.text(12, 82, "RECOVERY", Display::Cyan);
    screen.text(12, 102, "INITIALIZING STORAGE...");
    screen.present();
    NorFlash nor;
    BlockDevice disk(nor);
    Filesystem fs(disk);
    UsbMassStorage usb(disk);
    FatCheckOperation fatCheck(disk, screen);
    bool ready = nor.initialize() && disk.initialize();
    Watchdog::progress();
    // Before FatFs mounts: the dirty flag of an unfinished host session and the factory label.
    Watchdog::expect(RecoveryController::LongOperationMs);
    const bool reconciled = !ready || fatCheck.reconcile();
    if (ready) fs.mountPrepared();
    RecoveryController controller(screen, nor, fs, usb, update, fatCheck, disk, ready);
    if (!reconciled) controller.reportFatRepairFailure();
    controller.run();
}
