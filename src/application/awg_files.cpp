#include "openosc/application/awg_files.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/layout.hpp"
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace openosc {
using Layout = storage::Layout;
bool AwgFiles::supportedName(const char *name) {
    const size_t length = std::strlen(name);
    return length > 4 && length < NameBytes && !strcasecmp(name + length - 4, ".CSV");
}
bool AwgFiles::scan(unsigned first) {
    count_ = visible_ = 0; first_ = first;
    if (!fs_.mounted()) return fail("Storage not available");
    DIR directory{};
    FRESULT result = f_opendir(&directory, Layout::Awg);
    if (result == FR_NO_PATH) { // The first time: the directory a computer copies the files into.
        if (storage::ensureDirectory(Layout::Awg) != FR_OK) return fail("Cannot create /openosc/AWG");
        return true;
    }
    if (result != FR_OK) return fail("Cannot read /openosc/AWG");
    FILINFO info{};
    while ((result = f_readdir(&directory, &info)) == FR_OK && info.fname[0]) {
        if ((info.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !supportedName(info.fname)) continue;
        if (count_ >= first && visible_ < PageSize) {
            std::memcpy(names_[visible_], info.fname, std::strlen(info.fname) + 1); // supportedName: shorter than NameBytes.
            sizes_[visible_++] = uint32_t(info.fsize);
        }
        ++count_;
    }
    const FRESULT closed = f_closedir(&directory);
    return result == FR_OK && closed == FR_OK ? true : fail("Cannot read /openosc/AWG");
}
bool AwgFiles::refresh() {
    error_ = nullptr;
    if (!scan(selected_ / PageSize * PageSize)) return false;
    if (!count_) { selected_ = first_ = 0; return true; }
    if (selected_ >= count_) { selected_ = count_ - 1; return scan(selected_ / PageSize * PageSize); }
    return true;
}
bool AwgFiles::move(int direction) {
    if (!count_ || !direction || open_) return false;
    selected_ = direction > 0 ? (selected_ + 1) % count_ : (selected_ + count_ - 1) % count_;
    return selected_ / PageSize * PageSize == first_ || refresh();
}
ui::ImageList AwgFiles::list() const {
    ui::ImageList page;
    page.visible = visible_; page.selected = selected_; page.first = first_; page.total = count_;
    page.empty = "No CSV files"; page.location = Layout::Awg;
    for (unsigned i = 0; i < visible_; ++i) {
        page.names[i] = names_[i];
        const uint32_t bytes = sizes_[i];
        if (bytes < 1024) std::snprintf(values_[i], sizeof values_[i], "%lu B", (unsigned long)bytes);
        else std::snprintf(values_[i], sizeof values_[i], "%lu KiB", (unsigned long)((bytes + 1023) / 1024));
        page.values[i] = values_[i];
    }
    return page;
}
bool AwgFiles::open() {
    close();
    error_ = nullptr;
    if (!name()[0]) return fail("No file selected");
    if (sizes_[selected_ - first_] > generator::ArbitraryCsv::MaximumBytes) return fail("File larger than 64 KiB");
    char path[sizeof "/openosc/AWG/" + NameBytes];
    std::snprintf(path, sizeof path, "%s/%s", Layout::Awg, name());
    if (!fs_.mounted() || f_open(&file_, path, FA_READ) != FR_OK) return fail("Cannot open file");
    open_ = true;
    csv_.begin();
    return true;
}
AwgFiles::Step AwgFiles::read(generator::CodeSink &sink, uint32_t budget) {
    if (!open_) { fail("No file open"); return Step::Failed; }
    bool ok = true, end = false;
    while (ok && !end && budget) {
        UINT count = 0;
        const UINT want = UINT(budget < sizeof chunk_ ? budget : sizeof chunk_);
        if (f_read(&file_, chunk_, want, &count) != FR_OK) { close(); fail("File read failed"); return Step::Failed; }
        budget -= count;
        end = count < want;
        ok = csv_.feed(chunk_, count, sink);
    }
    if (ok && end) ok = csv_.finish(sink);
    if (!ok) {
        const auto e = csv_.error();
        if (e == generator::ArbitraryCsv::Error::TooFew || e == generator::ArbitraryCsv::Error::Size || e == generator::ArbitraryCsv::Error::Stage)
            std::snprintf(message_, sizeof message_, "Not loaded: %s", generator::ArbitraryCsv::text(e));
        else std::snprintf(message_, sizeof message_, "Line %lu: %s", (unsigned long)csv_.line(), generator::ArbitraryCsv::text(e));
        close();
        error_ = message_;
        return Step::Failed;
    }
    if (!end) return Step::More;
    close();
    return Step::Done;
}
void AwgFiles::close() {
    if (open_) f_close(&file_);
    open_ = false;
}
} // namespace openosc
