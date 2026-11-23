#pragma once
#include "openosc/display/display.hpp"
#include "openosc/recovery/catalog.hpp"
#include "openosc/update/image.hpp"
#include "openosc/storage/fat/checker.hpp"

namespace openosc {
class RecoveryScreens {
public:
    explicit RecoveryScreens(Display& screen) : screen_(screen) {}
    void menu(unsigned selected, uint32_t jedec, uint32_t bytes, const char* status);
    void files(const ImageCatalog& catalog, unsigned selected);
    void calibration(unsigned channel, bool hex);
    void confirm(const char* title, const char* detail, bool yes,
                 const ImageHeader* image = nullptr, bool rawBinary = false);
    void message(const char* title, const char* detail);
    void disconnect(bool yes);
    void backupResult(uint32_t crc);
    void powerHold(uint32_t current, uint32_t value, unsigned digit);
    void powerHoldFailed(const char* detail);
    void fatReport(const fat::Report& report);
    void fatRepairConfirm(const fat::Report& report, bool yes);
    void fatFormatConfirm(bool yes);
private:
    void frame(const char* title, const char* footer);
    Display& screen_;
};
}
