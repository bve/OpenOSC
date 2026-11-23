#include "openosc/platform/watchdog.hpp"
#include "openosc/platform/stack_guard.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/display/display.hpp"
#include "openosc/application/controller.hpp"
#include "openosc/application/boot_guard.hpp"
#include "openosc/application/boot_timing.hpp"
#include "openosc/update/crc32.hpp"
#include "openosc/update/layout.hpp"
#include <new>

namespace {
// BootGuard's setting: the run mode written through the normal settings file.
class RunModeSetting final : public openosc::BootGuard::Setting {
public:
    explicit RunModeSetting(openosc::PreferencesFile &file) : file_(file) {}
    bool saveRunMode(uint32_t mode) override {
        auto values = file_.values();
        values.scope.runMode = mode;
        return file_.save(values);
    }
private:
    openosc::PreferencesFile &file_;
};
} // namespace

int main() {
    using namespace openosc;
    StackGuard::arm(); // The lowest stack words, far below main's frame now; checked by the main loop.
    boot_splash::Style bootStyle;
    const bool bootFrame = Display::takeBootFrame(&bootStyle);
    if (!Board::initialize(bootFrame)) Watchdog::fatal(boot_diagnostics.error);
    BootTiming::mark(BootTiming::Start);
    BatteryMonitor battery;
    battery.initialize(); // Also configures the shared ADC12 clock.
    McuTemperature mcuTemperature;
    const auto startupTemperature = mcuTemperature.captureStartup();
    LvglDisplay display;
    const bool bootDisplayReady = display.showBootSplash(bootFrame, bootStyle);
    // Power-on keys: read now, before the NOR and FAT start (about 0.75 s on a ZT703S, SYST:BOOT?), not
    // at the settings.
    const uint32_t powerOnKeys = Keypad::read();
    boot_diagnostics.factoryCrc = Crc32::compute(reinterpret_cast<const void*>(layout::FactoryBase), layout::FactoryBytes);
    BootTiming::mark(BootTiming::Crc);
    NorFlash nor;
    BlockDevice disk(nor);
    Filesystem fs(disk);
    const bool norReady = nor.initialize();
    BootTiming::mark(BootTiming::Nor);
    const bool diskReady = norReady && disk.initialize();
    BootTiming::mark(BootTiming::Disk);
    if (diskReady) {
        fs.mountPrepared([] { BootTiming::mark(BootTiming::Fat); });
        boot_diagnostics.flashStatus=fs.result();
    }
    BootTiming::mark(BootTiming::Mounted);
    Watchdog::progress(); // Boot stages are the boot's progress (one pass, WatchdogPolicy::OperationMs).
    PreferencesFile preferences(fs);
    preferences.load();
    preferences.applyPowerOnKeys(powerOnKeys); // Before PE10, which follows the start mode.
    Board::configurePe10(preferences.values().mode == unsigned(MeasurementMode::Generator));
    BootTiming::mark(BootTiming::Settings);
    Watchdog::progress();
    // The 360 MHz profile only through its guard (BootGuard), before USB starts (STORE-02).
    BootGuard guard(fs);
    RunModeSetting setting(preferences);
    const auto start = guard.start(preferences.values().scope.runMode, setting);
    BootTiming::mark(BootTiming::Guard);
    Watchdog::progress();
    if (!ClockControl::selectPerformance(start.mode)) {
        boot_diagnostics.error=4;
        // Keep a usable instrument if the profile cannot be locked; no high-rate metadata is fabricated.
        if (!ClockControl::selectPerformance(0)) Watchdog::fatal(4);
    }
    BootTiming::mark(BootTiming::Clocks);
    if (bootDisplayReady) PowerControl::retime();
#ifdef OPENOSC_USB_TRACE
    // Both the DWT timestamp scale and TIM7 divider depend on the selected profile.
    usbtrace::start();
#endif
    ThemeStore themes(fs);
    ScreenshotWriter screenshots(fs);
    // The service lives for the complete firmware lifetime. It holds no DMA memory (the NOR, UART
    // and capture buffers are separate), so it lives in DTCM and AXI keeps room for the arena.
    alignas(DataAccess) static uint8_t dataStorage[sizeof(DataAccess)];
    DataAccess& data = *new (dataStorage) DataAccess(fs);
    data.meter().startupJunction(startupTemperature);
    ApplicationController controller(display, nor, fs, preferences, themes, screenshots, data, battery, mcuTemperature);
    controller.bootGuard(guard, start);
    controller.run();
}
