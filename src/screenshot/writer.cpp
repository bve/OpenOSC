#include "openosc/screenshot/writer.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/update/crc32.hpp"
#include <cstdio>

namespace openosc {
uint32_t ScreenshotWriter::number(const char* name) {
    const auto upper = [](char c) { return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c; };
    if (upper(name[0]) != 'S' || upper(name[1]) != 'C' || upper(name[2]) != 'R') return 0;
    uint32_t value = 0;
    for (unsigned i = 3; i < 8; ++i) {
        if (name[i] < '0' || name[i] > '9') return 0;
        value = value * 10 + uint32_t(name[i] - '0');
    }
    return name[8] == '.' || name[8] == '-' ? value : 0;
}
bool ScreenshotWriter::highestNumber(uint32_t& highest) {
    DIR directory{};
    if (f_opendir(&directory, storage::Layout::Screenshots) != FR_OK) return false;
    FILINFO info{};
    FRESULT result;
    while ((result = f_readdir(&directory, &info)) == FR_OK && info.fname[0]) {
        if (info.fattrib & AM_DIR) continue;
        const uint32_t value = number(info.fname);
        if (value > highest) highest = value;
    }
    return f_closedir(&directory) == FR_OK && result == FR_OK;
}
bool ScreenshotWriter::fail(const char* message) {
    if (open_) f_close(&file_);
    open_ = false; complete_ = false; error_ = message;
    return false;
}
bool ScreenshotWriter::start(const uint16_t* frame) {
    if (open_) return false;
    complete_ = false; error_ = nullptr; position_ = 0;
    if (!frame || !fs_.mounted() || !fs_.refreshFree()) return fail("STORAGE NOT AVAILABLE");
    if (fs_.freeBytes() < BmpImage::FileBytes + 8192) return fail("NOT ENOUGH FREE SPACE");
    FRESULT result = storage::ensureDirectory(storage::Layout::Screenshots);
    if (result != FR_OK) return fail("CANNOT CREATE SCREENSHOTS DIRECTORY");
    // Start after the highest numbered entry when there is room. Include the pixel CRC in the
    // filename to distinguish new content under a reused sequence number on FAT without a clock (#105).
    uint32_t highest = 0;
    if (!highestNumber(highest)) return fail("CANNOT CHECK SCREENSHOT NAME");
    constexpr uint32_t LastId = 99999;
    if (highest < LastId && nextId_ <= highest) nextId_ = highest + 1;
    const unsigned long crc = Crc32::compute(frame, BmpImage::PixelBytes);
    for (uint32_t attempt = 0; attempt < LastId; ++attempt) {
        if (nextId_ > LastId) nextId_ = 1;
        const auto id = static_cast<unsigned long>(nextId_++);
        std::snprintf(final_, sizeof final_, "%s/SCR%05lu-%08lX.BMP", storage::Layout::Screenshots, id, crc);
        std::snprintf(temporary_, sizeof temporary_, "%s/SCR%05lu.TMP", storage::Layout::Screenshots, id);
        FILINFO info{};
        result = f_stat(final_, &info);
        if (result == FR_OK) continue; // Never over an existing image.
        if (result != FR_NO_FILE) return fail("CANNOT CHECK SCREENSHOT NAME");
        result = f_open(&file_, temporary_, FA_WRITE | FA_CREATE_NEW);
        if (result == FR_EXIST) continue;
        if (result != FR_OK) return fail("CANNOT CREATE SCREENSHOT FILE");
        open_ = true;
        image_ = BmpImage(frame);
        return true;
    }
    return fail("SCREENSHOT NAME LIMIT REACHED");
}
void ScreenshotWriter::cancel() {
    if (!open_) return;
    f_close(&file_);
    f_unlink(temporary_);
    open_ = false; complete_ = false; error_ = "SCREENSHOT CANCELLED";
}
void ScreenshotWriter::service() {
    if (!open_) return;
    const size_t chunk = image_.read(position_, buffer_, sizeof buffer_);
    UINT written = 0;
    if (f_write(&file_, buffer_, chunk, &written) != FR_OK || written != chunk) {
        fail("SCREENSHOT WRITE FAILED"); return;
    }
    position_ += written;
    if (position_ != BmpImage::FileBytes) return;
    const FRESULT synced = f_sync(&file_), closed = f_close(&file_);
    open_ = false;
    if (synced != FR_OK || closed != FR_OK) { fail("SCREENSHOT SYNC FAILED"); return; }
    if (f_rename(temporary_, final_) != FR_OK) { fail("SCREENSHOT FINALIZE FAILED"); return; }
    complete_ = true;
}
}
