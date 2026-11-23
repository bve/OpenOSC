#include "openosc/storage/open_readers.hpp"
#include "openosc/screenshot/catalog.hpp"
#include <cstdio>
#include <cstring>

namespace openosc::screenshot {
bool ImageCatalog::image(const FILINFO &info) {
    if (info.fattrib&(AM_DIR|AM_HID|AM_SYS)) return false;
    const size_t length=std::strlen(info.fname);
    if (length<5 || length>=NameBytes || info.fname[length-4]!='.') return false;
    const auto upper=[](char c) { return c>='a' && c<='z' ? c-'a'+'A' : c; };
    return upper(info.fname[length-3])=='B' && upper(info.fname[length-2])=='M' && upper(info.fname[length-1])=='P';
}
bool ImageCatalog::fail(const char *message) { error_=message; return false; }
bool ImageCatalog::scan(unsigned first) {
    count_=visible_=0; first_=first;
    for (auto &entry:entries_) entry={};
    if (!fs_.mounted()) return fail("Storage not available");
    DIR directory{};
    auto result=f_opendir(&directory,Directory);
    if (result==FR_NO_PATH) return true;
    if (result!=FR_OK) return fail("Cannot read screenshot directory");
    FILINFO info{};
    while ((result=f_readdir(&directory,&info))==FR_OK && info.fname[0]) {
        if (!image(info)) continue;
        if (count_>=first && visible_<PageSize) {
            auto &entry=entries_[visible_++];
            std::snprintf(entry.name,sizeof entry.name,"%s",info.fname);
            entry.bytes=info.fsize;
        }
        ++count_;
    }
    const auto closed=f_closedir(&directory);
    return result==FR_OK && closed==FR_OK ? true : fail("Screenshot directory read failed");
}
bool ImageCatalog::refresh() {
    if (clearing_) return false;
    error_=nullptr;
    if (!scan(selected_/PageSize*PageSize)) return false;
    if (!count_) { selected_=first_=0; return true; }
    if (selected_>=count_) {
        selected_=count_-1;
        return scan(selected_/PageSize*PageSize);
    }
    return true;
}
bool ImageCatalog::move(int direction) {
    if (clearing_ || !count_ || !direction) return false;
    selected_=direction>0 ? (selected_+1)%count_ : (selected_+count_-1)%count_;
    return selected_/PageSize*PageSize==first_ || refresh();
}
bool ImageCatalog::path(char *output,size_t capacity) const {
    if (!output || !capacity || !name()[0]) return false;
    const int length=std::snprintf(output,capacity,"%s/%s",Directory,name());
    return length>0 && size_t(length)<capacity;
}
bool ImageCatalog::eraseSelected() {
    char target[PathBytes];
    if (!fs_.mounted()) return fail("Storage not available");
    if (clearing_ || !path(target,sizeof target)) return false;
    if (storage::openReaders) return fail("Image in use over USB"); // MMEM:DATA? reads a file.
    if (f_unlink(target)!=FR_OK) return fail("Cannot delete image");
    return refresh();
}
bool ImageCatalog::beginClear() {
    if (clearing_) return fail("Image cleanup already in progress");
    if (storage::openReaders) return fail("Image in use over USB");
    erased_=0;
    // Clear screenshots can start directly from Settings. Count the BMPs on disk before deleting any.
    if (!refresh()) return false;
    if (!count_) return true;
    const auto opened=f_opendir(&directory_,Directory);
    if (opened!=FR_OK) return fail("Cannot read screenshot directory");
    clearing_=true;
    return true;
}
void ImageCatalog::cancelClear() {
    if (clearing_) f_closedir(&directory_);
    clearing_=false;
}
bool ImageCatalog::clearStep() {
    if (!clearing_) return true;
    FILINFO info{};
    FRESULT result;
    while ((result=f_readdir(&directory_,&info))==FR_OK && info.fname[0]) {
        if (!image(info)) continue;
        char target[PathBytes];
        std::snprintf(target,sizeof target,"%s/%s",Directory,info.fname);
        if (f_unlink(target)!=FR_OK) { cancelClear(); return fail("Image cleanup stopped; delete failed"); }
        ++erased_;
        return true;
    }
    const auto closed=f_closedir(&directory_); clearing_=false;
    if (result!=FR_OK || closed!=FR_OK) return fail("Image cleanup stopped; read failed");
    selected_=0;
    return refresh();
}
} // namespace openosc::screenshot
