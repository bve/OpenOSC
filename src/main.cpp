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
    if (!Board::initialize()) Watchdog::fatal(boot_diagnostics.error);
    Board::delay(30);
    const bool requestedByApp = RecoveryRequest::take();
    // Recovery itself faulted last time (e.g. on an ECC-broken header after a cut update): no auto-start
    // into the same fault, the menu comes up. Taken once.
    auto &last = ResetRecord::retained();
    const bool faulted = ResetRecord::valid(last) && last.source == uint32_t(ResetRecord::Source::Recovery) &&
                         (last.cause == uint32_t(ResetRecord::Cause::Fault) || last.cause == uint32_t(ResetRecord::Cause::Interrupt));
    if (faulted) last.magic = 0;
    const bool recoveryRequested = (Keypad::read() & Keypad::F2) || requestedByApp || faulted;
    Display screen;
    UpdateOperation update(screen);
#ifndef OPENOSC_RAM_TEST
    if (!recoveryRequested && update.installed()) Handoff::jump(layout::VectorBase);
#else
    (void)recoveryRequested;
#endif
    boot_diagnostics.factoryCrc = Crc32::compute(reinterpret_cast<const void*>(layout::FactoryBase), layout::FactoryBytes);
    if (!screen.initialize()) {
        boot_diagnostics.error = 2;
        Watchdog::fatal(2);
    }
    boot_diagnostics.stage = 3;
    screen.text(8, 10, "OPENOSC RECOVERY", Display::Cyan);
    screen.text(8, 40, "INITIALIZING STORAGE...");
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
