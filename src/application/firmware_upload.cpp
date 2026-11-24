#include "openosc/application/firmware_upload.hpp"
#include "openosc/storage/layout.hpp"
#include <cstdio>
#include <cstring>

namespace openosc {
FIL FirmwareUpload::file_{};
char FirmwareUpload::name_[NameBytes]{}, FirmwareUpload::temporary_[PathBytes]{}, FirmwareUpload::final_[PathBytes]{};
void FirmwareUpload::cancel() {
    if (!open_) return;
    f_close(&file_); f_unlink(temporary_);
    open_ = abandoned_ = false; owner_ = nullptr;
}
bool FirmwareUpload::path(const char *name, char *out, size_t size) {
    const int length = std::snprintf(out, size, "%s/%s", storage::Layout::Update, name);
    return length > 0 && size_t(length) < size;
}
int FirmwareUpload::write(const void *owner, const char *name, uint32_t offset, const uint8_t *data, size_t size) {
    service();
    if (!offset) {
        if (open_ && owner_ != owner) return -221;
        cancel();
        if (!fs_.mounted()) return -200;
        if (!path(name, final_, sizeof final_)) return -224;
        std::snprintf(temporary_, sizeof temporary_, "%s.TMP", final_);
        if (f_open(&file_, temporary_, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return -200;
        open_ = true; owner_ = owner; written_ = 0; crc_ = Crc32(); std::strcpy(name_, name);
    } else if (!open_ || owner_ != owner || offset != written_ || std::strcmp(name, name_)) {
        if (owner_ == owner) cancel();
        return -222;
    }
    UINT done = 0;
    if (f_write(&file_, data, UINT(size), &done) != FR_OK || done != size) { cancel(); return -200; } // Also a full volume.
    crc_.update(data, size); written_ += uint32_t(size);
    return 0;
}
int FirmwareUpload::end(const void *owner, uint32_t &bytes, uint32_t &crc) {
    service();
    if (!open_ || owner_ != owner) return -221;
    const FRESULT closed = f_close(&file_);
    open_ = false; owner_ = nullptr;
    const FRESULT removed = closed == FR_OK ? f_unlink(final_) : closed;
    if ((removed != FR_OK && removed != FR_NO_FILE) || f_rename(temporary_, final_) != FR_OK) { f_unlink(temporary_); return -200; }
    bytes = written_; crc = crc_.value();
    return 0;
}
} // namespace openosc
