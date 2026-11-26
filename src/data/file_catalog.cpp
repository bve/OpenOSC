#include "openosc/data/file_catalog.hpp"
#include <cstdio>
#include <cstring>

namespace openosc {
bool FileCatalog::listable(const FILINFO &info) const {
    if (info.fattrib & (AM_DIR | AM_HID | AM_SYS)) return false;
    const size_t length = std::strlen(info.fname);
    return length && length < NameBytes && !std::strpbrk(info.fname, "/\\") && profile_.accepts(info.fname);
}
bool FileCatalog::scan(unsigned first) {
    count_ = visible_ = 0; first_ = first; listed_ = false;
    for (auto &entry : entries_) entry = {};
    if (!fs_.mounted()) return fail("Storage not available");
    DIR directory{};
    auto result = f_opendir(&directory, profile_.directory);
    if (result == FR_NO_PATH) { listed_ = true; return true; } // Not made yet: nothing saved.
    if (result != FR_OK) return fail(profile_.messages.open);
    FILINFO info{};
    while ((result = f_readdir(&directory, &info)) == FR_OK && info.fname[0]) {
        if (!listable(info)) continue;
        if (count_ >= first && visible_ < PageSize) {
            auto &entry = entries_[visible_++];
            std::snprintf(entry.name, sizeof entry.name, "%s", info.fname);
            entry.bytes = uint32_t(info.fsize);
        }
        ++count_;
    }
    const auto closed = f_closedir(&directory);
    if (result == FR_OK && closed == FR_OK) { listed_ = true; return true; }
    count_ = visible_ = 0; // A part of the directory is never shown as all of it.
    for (auto &entry : entries_) entry = {};
    return fail(profile_.messages.read);
}
bool FileCatalog::refresh() {
    if (clearing_) return false;
    error_ = nullptr;
    if (!scan(selected_ / PageSize * PageSize)) return false;
    if (!count_) { selected_ = first_ = 0; return true; }
    if (selected_ >= count_) {
        selected_ = count_ - 1;
        return scan(selected_ / PageSize * PageSize);
    }
    return true;
}
bool FileCatalog::move(int direction) {
    if (clearing_ || !count_ || !direction) return false;
    selected_ = direction > 0 ? (selected_ + 1) % count_ : (selected_ + count_ - 1) % count_;
    return selected_ / PageSize * PageSize == first_ || refresh();
}
bool FileCatalog::path(char *output, size_t capacity) const {
    if (!output || !capacity || !name()[0]) return false;
    const int length = std::snprintf(output, capacity, "%s/%s", profile_.directory, name());
    return length > 0 && size_t(length) < capacity;
}
bool FileCatalog::eraseSelected() {
    char target[PathBytes];
    if (!fs_.mounted()) return fail("Storage not available");
    if (clearing_ || !path(target, sizeof target)) return false;
    if (inUse()) return fail(profile_.messages.inUse); // MMEM:DATA? reads one.
    // Still the plain file that was listed: a name that became a directory is never removed.
    FILINFO info{};
    if (f_stat(target, &info) != FR_OK || !listable(info) || f_unlink(target) != FR_OK) {
        refresh();
        return fail(profile_.messages.erase);
    }
    return refresh();
}
bool FileCatalog::beginClear() {
    if (clearing_) return fail(profile_.messages.clearing);
    if (inUse()) return fail(profile_.messages.inUse);
    erased_ = 0;
    // A clear can start directly from Settings. Count the files before deleting any.
    if (!refresh()) return false;
    if (!count_) return true;
    if (f_opendir(&directory_, profile_.directory) != FR_OK) return fail(profile_.messages.open);
    clearing_ = true;
    return true;
}
void FileCatalog::cancelClear() {
    if (clearing_) f_closedir(&directory_);
    clearing_ = false;
}
bool FileCatalog::clearStep() {
    if (!clearing_) return true;
    if (inUse()) { cancelClear(); return fail(profile_.messages.clearInUse); }
    FILINFO info{};
    FRESULT result;
    while ((result = f_readdir(&directory_, &info)) == FR_OK && info.fname[0]) {
        if (!listable(info)) continue;
        char target[PathBytes];
        const int length = std::snprintf(target, sizeof target, "%s/%s", profile_.directory, info.fname);
        if (length <= 0 || size_t(length) >= sizeof target || f_unlink(target) != FR_OK) {
            cancelClear();
            return fail(profile_.messages.clearErase);
        }
        ++erased_;
        return true;
    }
    const auto closed = f_closedir(&directory_); clearing_ = false;
    if (result != FR_OK || closed != FR_OK) return fail(profile_.messages.clearRead);
    selected_ = 0;
    return refresh();
}
} // namespace openosc
