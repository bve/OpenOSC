#include "openosc/storage/filesystem.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/layout.hpp"

namespace openosc {
bool Filesystem::mount(bool count) {
    if (disk_.owner() != MediaOwner::Local) { result_ = FR_NOT_READY; return false; }
    registerFatDevice(&disk_);
    result_ = f_mount(&fs_, "", 1);
    mounted_ = result_ == FR_OK;
    if (mounted_ && count) refreshFree();
    return mounted_;
}
bool Filesystem::unmount() {
    if (!mounted_) return disk_.flush(); // Raw writes (formatter, FAT repair) may be cached; else no NOR access.
    if (!disk_.sync()) return false;
    result_ = f_mount(nullptr, "", 0);
    if (result_ == FR_OK) mounted_ = false;
    return result_ == FR_OK;
}
bool Filesystem::prepareDirectories() {
    result_ = storage::prepareLayout(); // Empty factory root directories out, the whole /openosc tree in.
    if (result_ == FR_OK) return refreshFree();
    const FRESULT failed = result_;
    refreshFree(); // The one free count of mountPrepared, also when the layout failed.
    result_ = failed;
    return false;
}
bool Filesystem::refreshFree() {
    if (!mounted_) return false;
    DWORD clusters = 0;
    FATFS* fs = nullptr;
    result_ = f_getfree("", &clusters, &fs);
    if (result_ != FR_OK) return false;
    uint64_t bytes = uint64_t(clusters) * fs->csize * disk_.sectorSize();
    if (bytes > uint64_t(disk_.sectorCount()) * disk_.sectorSize()) { result_ = FR_INT_ERR; return false; }
    freeBytes_ = bytes;
    return true;
}
}
