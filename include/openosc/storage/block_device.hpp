#pragma once
#include "openosc/storage/nor_flash.hpp"

namespace openosc {
enum class MediaOwner : uint8_t { Local, Host };

class BlockDevice {
public:
    explicit BlockDevice(NorFlash& nor) : nor_(nor) {}
    bool initialize();
    bool read(uint32_t lba, uint32_t count, uint8_t* data, MediaOwner requester);
    bool write(uint32_t lba, uint32_t count, const uint8_t* data, MediaOwner requester);
    bool sync(); // The cached block written, then the NOR idle.
    bool flush(); // The cached block written; no NOR access when nothing is cached.
    // The idle pulse: a block dirty for PulseMs is written while `idle` (nobody using the device, the
    // caller's call) and writes have paused for QuietMs. Syncs flush whatever the state. True: written.
    bool service(uint32_t now, bool idle);
    static constexpr uint32_t QuietMs = 500, PulseMs = 30000;
    uint32_t sectorSize() const { return sectorSize_; }
    uint32_t sectorCount() const { return nor_.geometry().capacity / sectorSize_; }
    MediaOwner owner() const { return owner_; }
    bool setOwner(MediaOwner owner); // Flushes first (false: still Local). The host (its interrupt) writes through.
    const char* error() const { return nor_.error(); }
private:
    bool valid(uint32_t lba, uint32_t count, MediaOwner requester) const;
    bool writeBlock(uint32_t physical);
    NorFlash& nor_;
    uint32_t sectorSize_ = 512;
    volatile MediaOwner owner_ = MediaOwner::Local;
    // Local writes: one 4 KiB erase block kept in RAM, so its sectors cost one erase, not one each.
    alignas(4) uint8_t scratch_[4096];
    uint32_t cached_ = 0, dirtyAt_ = 0, writtenAt_ = 0;
    bool cacheValid_ = false, dirty_ = false;
};
void registerFatDevice(BlockDevice* device);
}

