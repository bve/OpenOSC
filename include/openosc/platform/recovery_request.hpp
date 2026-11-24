#pragma once
#include <cstdint>

namespace openosc {
// The application's request to Recovery, kept in DTCM across the jump (0x2001FFC0..0x20020000, 64 bytes):
// magic, ~magic, kind (0 menu, 1 install an image and start it), the CRC-32 of kind and name, the name.
class RecoveryRequest {
public:
    static constexpr uint32_t Address = 0x2001ffc0, Magic = 0x52454356;
    static constexpr unsigned NameBytes = 48;
    struct Request {
        bool recovery = false; // The application asked for Recovery (the menu, or `install`).
        bool install = false;  // Install /openosc/UPDATE/<name> and start it (a record whose CRC fails is only the menu).
        char name[NameBytes]{};
    };
    // Consumes the request: the whole record is cleared before anything else is looked at.
    static Request take();
    // install: the file name in /openosc/UPDATE to install and start, or nullptr for the menu.
    [[noreturn]] static void restart(const char *install = nullptr);
};
}
