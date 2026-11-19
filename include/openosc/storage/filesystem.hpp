#pragma once
#include "openosc/storage/block_device.hpp"
#include "ff.h"

namespace openosc {
class Filesystem {
public:
    explicit Filesystem(BlockDevice& disk) : disk_(disk) {}
    bool mount();
    bool unmount();
    bool prepareDirectories(); // storage::prepareLayout(): empty factory root dirs removed, /openosc tree.
    bool mountPrepared() { return mount() && prepareDirectories(); } // Every mount OpenOSC uses.
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

