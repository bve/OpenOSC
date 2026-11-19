#pragma once
#include "openosc/storage/block_device.hpp"
#include "ff.h"

namespace openosc {
class Filesystem {
public:
    explicit Filesystem(BlockDevice& disk) : disk_(disk) {}
    bool mount(bool count = true);
    bool unmount();
    bool prepareDirectories(); // storage::prepareLayout(): empty factory root dirs removed, /openosc tree.
    // Every mount OpenOSC uses; `mounted` after f_mount either way (boot mark; flashStatus tells failure).
    bool mountPrepared(void (*mounted)() = nullptr) {
        const bool ok = mount(false);
        if (mounted) mounted();
        return ok && prepareDirectories();
    }
    bool mounted() const { return mounted_; }
    uint32_t freeBytes() const { return freeBytes_; }
    FRESULT result() const { return result_; }
    bool refreshFree();
    bool service(uint32_t now, bool idle) { return disk_.service(now, idle); } // The block cache's idle pulse.
private:
    BlockDevice& disk_;
    FATFS fs_{};
    uint32_t freeBytes_ = 0;
    FRESULT result_ = FR_NOT_READY;
    bool mounted_ = false;
};
}

