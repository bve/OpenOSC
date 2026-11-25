#pragma once
#include <cstdint>

namespace openosc {
// The power-on hold time of the factory bootloader: how long the power button must stay down after
// switching on (a countdown in ms, loaded by the bootloader's main() with one instruction). It lives in
// the bootloader's sector layout::FactoryBootSector, which is rewritten as a whole.
class PowerHold {
public:
    // Zero must never be written: the countdown would wrap and the power latch would never be set.
    static constexpr uint32_t MinMs = 1, MaxMs = 65535;
    // The value the factory bootloader in Flash holds now; false when this is not a bootloader we know.
    static bool read(uint32_t& ms);
    // Copies the sector, checks the whole bootloader in Flash by its CRC-32 and the instruction in the copy,
    // and patches the value into the copy.
    bool prepare(uint32_t ms);
    // Erases the sector, writes the prepared copy and verifies it; may be repeated after a failure.
    bool write();
    const char* error() const { return error_; }
private:
    bool fail(const char* message);
    const char* error_ = nullptr;
};
}
