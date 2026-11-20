#include "openosc/storage/directories.hpp"
#include "openosc/storage/layout.hpp"
#include <cstring>

namespace openosc::storage {
namespace {
// f_mkdir in a frame of its own: inlined (LTO), its name buffer and directory objects sat in
// ensureDirectory's frame under every f_stat it calls (the stack's deepest chains go through here).
__attribute__((noinline)) FRESULT makeDirectory(const char *path) { return f_mkdir(path); }
// One component: created if missing; FR_EXIST if something other than a directory is there.
FRESULT component(const char *path) {
    FILINFO info{};
    const FRESULT found = f_stat(path, &info);
    if (found == FR_OK) return (info.fattrib & AM_DIR) ? FR_OK : FR_EXIST;
    if (found != FR_NO_FILE && found != FR_NO_PATH) return found;
    const FRESULT made = makeDirectory(path);
    if (made != FR_OK && made != FR_EXIST) return made;
    return f_stat(path, &info) == FR_OK && (info.fattrib & AM_DIR) ? FR_OK : FR_EXIST;
}
}
FRESULT ensureDirectory(const char *directory) {
    if (!directory || directory[0] != '/' || !directory[1]) return FR_INVALID_NAME;
    char path[64];
    const size_t length = std::strlen(directory);
    if (length >= sizeof path) return FR_INVALID_NAME;
    std::memcpy(path, directory, length + 1);
    for (size_t i = 1; i <= length; ++i) {
        if (path[i] != '/' && path[i] != 0) continue;
        const char saved = path[i];
        path[i] = 0;
        const FRESULT result = component(path);
        path[i] = saved;
        if (result != FR_OK) return result;
    }
    return FR_OK;
}
namespace {
// FR_DENIED from f_unlink is a non-empty (or read-only) directory: kept. A file of that name is kept.
FRESULT removeEmptyFactoryDirectory(const char *path) {
    FILINFO info{};
    const FRESULT found = f_stat(path, &info);
    if (found == FR_NO_FILE) return FR_OK;
    if (found != FR_OK) return found;
    if (!(info.fattrib & AM_DIR)) return FR_OK;
    const FRESULT removed = f_unlink(path);
    return removed == FR_DENIED ? FR_OK : removed;
}
}
FRESULT prepareLayout() {
    for (const char *directory : Layout::FactoryRootDirectories) {
        const FRESULT result = removeEmptyFactoryDirectory(directory);
        if (result != FR_OK) return result;
    }
    for (const char *directory : Layout::Directories) {
        const FRESULT result = ensureDirectory(directory);
        if (result != FR_OK) return result;
    }
    return FR_OK;
}
} // namespace openosc::storage
