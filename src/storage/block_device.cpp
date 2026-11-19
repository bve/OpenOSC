#include "openosc/platform/watchdog.hpp"
#include "openosc/storage/block_device.hpp"
#include "openosc/platform/board.hpp"
#include <cstring>

namespace openosc {
namespace {
uint16_t little16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t little32(const uint8_t* p) { return little16(p) | (uint32_t(little16(p + 2)) << 16); }
uint32_t bootSectorSize(const uint8_t* p) {
    if (p[510] != 0x55 || p[511] != 0xaa) return 0;
    unsigned bytes = little16(p + 11), cluster = p[13];
    if (bytes < 512 || bytes > 4096 || (bytes & (bytes - 1))) return 0;
    if (!cluster || (cluster & (cluster - 1)) || !little16(p + 14) || !p[16]) return 0;
    return bytes;
}
}

bool BlockDevice::initialize() {
    cacheValid_ = dirty_ = false; // scratch_ reads the boot sector here.
    if (!nor_.read(0, scratch_, 512)) return false;
    if (uint32_t size = bootSectorSize(scratch_)) { sectorSize_ = size; return true; }
    // Preserve a valid existing MBR; inspect its partition using candidate LBA units.
    if (scratch_[510] == 0x55 && scratch_[511] == 0xaa) {
        uint32_t first = little32(scratch_ + 454);
        for (uint32_t size = 512; size <= 4096; size <<= 1) {
            uint64_t offset = uint64_t(first) * size;
            if (first && offset + 512 <= nor_.geometry().capacity
                && nor_.read(offset, scratch_, 512) && bootSectorSize(scratch_) == size) {
                sectorSize_ = size;
                return true;
            }
        }
    }
    sectorSize_ = 512;
    return true; // No formatting: expose the actual raw medium to the host.
}
bool BlockDevice::writeBlock(uint32_t physical) {
    // A failure after the erase leaves the block blank: write the whole image again.
    bool written = false;
    for (unsigned attempt = 0; attempt < 3 && !written; ++attempt) {
        written = nor_.writeSector(physical, scratch_);
        if (!written) Watchdog::progress(); // Each attempt may wait out ready()'s 5 s; the policy allows one.
    }
    if (!written) return false;
    Watchdog::progress(); // A NOR sector rewritten: progress of whatever file operation asked for it.
    Board::service();
    return true;
}
bool BlockDevice::flush() {
    if (!cacheValid_ || !dirty_) return true;
    if (!writeBlock(cached_)) return false; // Still dirty: the next flush tries again.
    dirty_ = false;
    return true;
}
bool BlockDevice::setOwner(MediaOwner owner) {
    if (owner == owner_) return true;
    if (!flush()) return false; // The dirty block stays, and so does the owner.
    cacheValid_ = false; // The host reads and writes the NOR itself.
    owner_ = owner;
    return true;
}
bool BlockDevice::service(uint32_t now, bool idle) {
    if (!idle || !dirty_ || owner_ != MediaOwner::Local) return false;
    if (int32_t(now - dirtyAt_) < int32_t(PulseMs) || int32_t(now - writtenAt_) < int32_t(QuietMs)) return false;
    if (flush()) return true;
    dirtyAt_ = now; // A failing NOR: the next pulse in PulseMs, not every pass (the next sync reports it).
    return false;
}

bool BlockDevice::valid(uint32_t lba, uint32_t count, MediaOwner requester) const {
    return requester == owner_ && lba <= sectorCount() && count <= sectorCount() - lba;
}
bool BlockDevice::read(uint32_t lba, uint32_t count, uint8_t* data, MediaOwner requester) {
    if (!valid(lba, count, requester) || (!data && count)) return false;
    if (!count) return true;
    const uint32_t address = lba * sectorSize_, bytes = count * sectorSize_;
    if (!nor_.read(address, data, bytes)) return false;
    if (cacheValid_ && dirty_ && address < cached_ + 4096 && cached_ < address + bytes) { // Newer than the NOR.
        const uint32_t from = address > cached_ ? address : cached_, to = address + bytes < cached_ + 4096 ? address + bytes : cached_ + 4096;
        std::memcpy(data + (from - address), scratch_ + (from - cached_), to - from);
    }
    return true;
}
bool BlockDevice::write(uint32_t lba, uint32_t count, const uint8_t* data, MediaOwner requester) {
    if (!valid(lba, count, requester) || (!data && count)) return false;
    uint32_t address = lba * sectorSize_;
    uint32_t remaining = count * sectorSize_;
    while (remaining) {
        const uint32_t physical = address & ~4095U;
        const uint32_t offset = address & 4095U;
        uint32_t chunk = 4096 - offset;
        if (chunk > remaining) chunk = remaining;
        if (owner_ != MediaOwner::Local) { // The host (in its interrupt): write-through, off the cache state.
            if (!nor_.read(physical, scratch_, sizeof scratch_)) return false;
            std::memcpy(scratch_ + offset, data, chunk);
            if (!writeBlock(physical)) return false;
            address += chunk; data += chunk; remaining -= chunk;
            continue;
        }
        if (!cacheValid_ || cached_ != physical) { // Another block: the cached one goes first (writes stay in order).
            if (!flush()) return false;
            cacheValid_ = false;
            if (chunk < 4096 && !nor_.read(physical, scratch_, sizeof scratch_)) return false;
            cached_ = physical; cacheValid_ = true;
        }
        std::memcpy(scratch_ + offset, data, chunk);
        const uint32_t now = Board::milliseconds();
        if (!dirty_) dirtyAt_ = now;
        dirty_ = true; writtenAt_ = now;
        address += chunk;
        data += chunk;
        remaining -= chunk;
    }
    return true;
}
bool BlockDevice::sync() { return flush() && nor_.ready(); }
}

