#include "openosc/update/internal_flash.hpp"
#include "openosc/update/layout.hpp"
#include "openosc/platform/board.hpp"

namespace openosc {
namespace {
constexpr uint32_t Controller = 0x52002000, ErrorMask = 0x17ee0000;
constexpr uint32_t EccErrors = 0x06000000; // SNECCERR, DBECCERR: raised by reads, not by an erase or write.
volatile uint32_t& reg(uint32_t address) { return *reinterpret_cast<volatile uint32_t*>(address); }
uint32_t controller(uint32_t destination) { return Controller + (destination >= 0x08100000 ? 0x100 : 0); }
bool valid(uint32_t address, size_t size) {
    return address >= layout::ApplicationBase && address <= layout::FlashEnd
        && size <= layout::FlashEnd - address;
}
}
bool InternalFlash::fail(const char* message) { error_ = message; return false; }
bool InternalFlash::wait(uint32_t address) {
    const uint32_t base = controller(address), start = Board::milliseconds();
    while (reg(base + 0x10) & 7) {
        Board::service();
        if (Board::milliseconds() - start > 3000) return fail("INTERNAL FLASH TIMEOUT");
    }
    const uint32_t status = reg(base + 0x10);
    boot_diagnostics.flashStatus = status;
    reg(base + 0x14) = status & (ErrorMask | 0x10000);
    return !(status & ErrorMask & ~EccErrors) || fail("INTERNAL FLASH STATUS ERROR");
}
bool InternalFlash::unlock(uint32_t address) {
    const uint32_t base = controller(address);
    if (reg(base + 0x0c) & 1) {
        reg(base + 4) = 0x45670123;
        reg(base + 4) = 0xcdef89ab;
    }
    if (reg(base + 0x0c) & 1) return fail("INTERNAL FLASH IS LOCKED");
    if (!(reg(base + 0x10) & 7)) reg(base + 0x14) = ErrorMask | 0x10000; // Flags left by earlier reads.
    return wait(address);
}
bool InternalFlash::eraseSector(uint32_t address) {
    if ((address & (layout::SectorBytes - 1)) || !valid(address, layout::SectorBytes))
        return fail("PROTECTED FLASH ERASE REJECTED");
    if (!unlock(address)) return false;
    const uint32_t base = controller(address);
    const uint32_t bankStart = address >= 0x08100000 ? 0x08100000 : 0x08000000;
    const uint32_t sector = (address - bankStart) / layout::SectorBytes;
    reg(base + 0x0c) = 4 | (sector << 6);
    reg(base + 0x0c) |= 0x20;
    bool result = wait(address);
    reg(base + 0x0c) &= ~(4U | 0x20U);
    return result;
}
bool InternalFlash::program(uint32_t address, const void* data, size_t size) {
    if ((address & 15) || (size & 15) || !valid(address, size) || (!data && size))
        return fail("PROTECTED FLASH WRITE REJECTED");
    const auto* source = static_cast<const uint32_t*>(data);
    while (size) {
        if (!unlock(address)) return false;
        const uint32_t base = controller(address);
        reg(base + 0x0c) = 2;
        auto* destination = reinterpret_cast<volatile uint32_t*>(address);
        for (unsigned i = 0; i < 4; ++i) destination[i] = source[i];
        __DSB(); __ISB();
        bool result = wait(address);
        reg(base + 0x0c) &= ~2U;
        if (!result) return false;
        for (unsigned i = 0; i < 4; ++i)
            if (destination[i] != source[i]) return fail("INTERNAL FLASH READBACK FAILED");
        source += 4;
        address += 16;
        size -= 16;
        Board::service();
    }
    return true;
}
void InternalFlash::lock() {
    reg(Controller + 0x0c) |= 1;
    reg(Controller + 0x10c) |= 1;
}
}

