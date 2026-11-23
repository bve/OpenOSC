#include "openosc/recovery/power_hold.hpp"
#include "openosc/update/internal_flash.hpp"
#include "openosc/update/layout.hpp"
#include <cstring>

namespace openosc {
namespace {
// Offsets inside the sector of the factory bootloader's main(): `mov r0, #500; ldr r1, [pc, #0x2F0];
// str r0, [r1]` stores the countdown to the word named by the literal at 0x3A8.
constexpr uint32_t ValueOffset = 0xb0, StoreOffset = 0xb4, LiteralOffset = 0x3a8;
constexpr uint8_t Store[] = {0xbc, 0x49, 0x08, 0x60};
constexpr uint32_t CountdownAddress = 0x20000030;
alignas(16) uint8_t sector[layout::SectorBytes];

uint32_t halfword(const uint8_t* data, uint32_t offset) { return data[offset] | (data[offset + 1] << 8); }
uint32_t word(const uint8_t* data, uint32_t offset) { return halfword(data, offset) | (halfword(data, offset + 2) << 16); }
// ThumbExpandImm of the 12-bit modified immediate of mov.w.
uint32_t expand(uint32_t imm12) {
    const uint32_t byte = imm12 & 0xff;
    if (!(imm12 >> 10)) {
        switch ((imm12 >> 8) & 3) {
        case 0: return byte;
        case 1: return byte | (byte << 16);
        case 2: return (byte << 8) | (byte << 24);
        default: return byte * 0x01010101U;
        }
    }
    const uint32_t value = 0x80 | (imm12 & 0x7f), rotation = imm12 >> 7;
    return (value >> rotation) | (value << (32 - rotation));
}
// The value the sector's main() loads into r0, or false for any other bootloader.
bool decode(const uint8_t* data, uint32_t& ms) {
    if (std::memcmp(data + StoreOffset, Store, sizeof Store) || word(data, LiteralOffset) != CountdownAddress)
        return false;
    const uint32_t first = halfword(data, ValueOffset), second = halfword(data, ValueOffset + 2);
    const uint32_t i = (first >> 10) & 1, imm3 = (second >> 12) & 7, imm8 = second & 0xff;
    if (second & 0x8f00) return false; // The second halfword of a data-processing immediate with Rd = r0.
    uint32_t value;
    if ((first & 0xfbf0) == 0xf240) value = ((first & 0xf) << 12) | (i << 11) | (imm3 << 8) | imm8; // movw r0, #imm16
    else if ((first & 0xfbff) == 0xf04f) value = expand((i << 11) | (imm3 << 8) | imm8);              // mov.w r0, #const
    else return false;
    ms = value;
    return value >= PowerHold::MinMs && value <= PowerHold::MaxMs;
}
// movw r0, #ms, little-endian halfwords.
void encode(uint8_t* data, uint32_t ms) {
    const uint32_t first = 0xf240 | (((ms >> 11) & 1) << 10) | ((ms >> 12) & 0xf);
    const uint32_t second = (((ms >> 8) & 7) << 12) | (ms & 0xff);
    data[0] = first; data[1] = first >> 8; data[2] = second; data[3] = second >> 8;
}
class FlashGuard {
public:
    explicit FlashGuard(InternalFlash& flash) : flash_(flash) {}
    ~FlashGuard() { flash_.lock(); }
private:
    InternalFlash& flash_;
};
}
bool PowerHold::fail(const char* message) { error_ = message; return false; }
bool PowerHold::read(uint32_t& ms) {
    return decode(reinterpret_cast<const uint8_t*>(layout::FactoryBootSector), ms);
}
bool PowerHold::prepare(uint32_t ms) {
    uint32_t current;
    std::memcpy(sector, reinterpret_cast<const void*>(layout::FactoryBootSector), sizeof sector);
    if (!decode(sector, current)) return fail("FACTORY BOOTLOADER NOT SUPPORTED");
    if (ms < MinMs || ms > MaxMs) return fail("VALUE OUT OF RANGE");
    encode(sector + ValueOffset, ms);
    return true;
}
bool PowerHold::write() {
    InternalFlash flash;
    flash.allowFactoryBootSector();
    FlashGuard guard(flash);
    if (!flash.eraseSector(layout::FactoryBootSector) || !flash.program(layout::FactoryBootSector, sector, sizeof sector))
        return fail(flash.error());
    if (std::memcmp(reinterpret_cast<const void*>(layout::FactoryBootSector), sector, sizeof sector))
        return fail("FACTORY BOOT READBACK FAILED");
    error_ = nullptr;
    return true;
}
}
