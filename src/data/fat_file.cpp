#include "openosc/data/fat_file.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/data/status.hpp"
#include "openosc/platform/board.hpp"
#include <climits>
#include <cstdio>
#include <cstring>

namespace openosc {
FatDataFile::~FatDataFile() { cancel(); }
void FatDataFile::cancel(bool keep) {
    if (open_ || verifying_) {
        f_close(&file_);
        if (!keep && f_unlink(temporary_) == FR_OK) staged_ = false;
    }
    open_ = verifying_ = false;
}
void FatDataFile::discard() {
    cancel(true);
    if (staged_ && f_unlink(temporary_) == FR_OK) staged_ = false;
}
bool FatDataFile::sync() {
    if (!open_) return false;
    if (f_sync(&file_) != FR_OK) return fail(DataStatus::IoError);
    return true;
}
bool FatDataFile::fail(DataStatus status) {
    cancel(true); // An I/O error keeps what was written.
    status_ = status;
    return false;
}
bool FatDataFile::begin(const char *directory, const char *prefix, const char *extension, uint64_t reserve) {
    if (open_ || verifying_)
        return false;
    staged_ = false; // Ownership is per attempt, including if a previous attempt kept its temporary file.
    if (!directory || !prefix || !extension || std::strlen(directory) > 40 || std::strlen(prefix) > 12 ||
        std::strlen(extension) > 8)
        return fail(DataStatus::InvalidArgument);
    if (!fs_.mounted() || !fs_.refreshFree())
        return fail(DataStatus::IoError);
    if (fs_.freeBytes() < FreeHeadroom || reserve > fs_.freeBytes() - FreeHeadroom)
        return fail(DataStatus::NoSpace);
    if (storage::ensureDirectory(directory) != FR_OK) // The branch again if it went missing after the mount.
        return fail(DataStatus::IoError);
    FILINFO info{};
    for (unsigned n = 1; n <= 99999; ++n) {
        std::snprintf(final_, sizeof final_, "%s/%s%05u.%s", directory, prefix, n, extension);
        std::snprintf(temporary_, sizeof temporary_, "%s/%s%05u.TMP", directory, prefix, n);
        const auto exists = f_stat(final_, &info);
        if (exists == FR_OK)
            continue;
        if (exists != FR_NO_FILE)
            return fail(DataStatus::IoError);
        const auto opened = f_open(&file_, temporary_, FA_WRITE | FA_CREATE_NEW);
        if (opened == FR_EXIST)
            continue;
        if (opened != FR_OK)
            return fail(DataStatus::IoError);
        open_ = staged_ = true;
        bytes_ = 0;
        crc_ = Crc32{};
        status_ = DataStatus::Ok;
        return true;
    }
    return fail(DataStatus::Overflow);
}
bool FatDataFile::write(const void *data, size_t bytes) {
    if (!open_ || (!data && bytes) || bytes > UINT_MAX)
        return fail(DataStatus::InvalidArgument);
    UINT count = 0;
    if (f_write(&file_, data, bytes, &count) != FR_OK)
        return fail(DataStatus::IoError);
    if (count != bytes) // FatFs found no free cluster.
        return fail(DataStatus::NoSpace);
    crc_.update(data, bytes);
    bytes_ += bytes;
    return true;
}
bool FatDataFile::writeWithinHeadroom(const void *data, size_t bytes) {
    if (!open_) return fail(DataStatus::InvalidArgument);
    if (bytes > UINT_MAX) return fail(DataStatus::InvalidArgument);
    if (!fs_.refreshFree()) return fail(DataStatus::IoError); // FatFs keeps the count: no FAT scan after the first.
    const FATFS *fs = file_.obj.fs;
#if FF_MAX_SS != FF_MIN_SS
    const uint64_t cluster = uint64_t(fs->csize) * fs->ssize;
#else
    const uint64_t cluster = uint64_t(fs->csize) * FF_MAX_SS;
#endif
    const uint64_t free = fs_.freeBytes();
    if (!cluster) return fail(DataStatus::IoError);
    const uint64_t tail = bytes_ % cluster;
    const uint64_t added = (tail + bytes + cluster - 1) / cluster - (tail != 0);
    // FAT may need another directory cluster to publish the long name.
    if (free < FreeHeadroom || (added + 1) * cluster > free - FreeHeadroom) return fail(DataStatus::NoSpace);
    return write(data, bytes);
}
DataStatus FatDataFile::commitStep() {
    if (open_) {
        const auto synced = f_sync(&file_), closed = f_close(&file_);
        open_ = false;
        if (synced != FR_OK || closed != FR_OK) {
            fail(DataStatus::IoError);
            return status_;
        }
        if (f_open(&file_, temporary_, FA_READ) != FR_OK) {
            fail(DataStatus::IoError);
            return status_;
        }
        verifying_ = true;
        verifiedBytes_ = 0;
        verified_ = Crc32{};
        if (f_size(&file_) != bytes_) {
            fail(DataStatus::IoError);
            return status_;
        }
    }
    if (!verifying_)
        return status_;
    uint8_t buffer[512];
    for (unsigned n = 0; n < 8 && verifiedBytes_ != bytes_; ++n) { // 4 KiB a pass: 512 B made a 1 MiB log outlast FinishMs.
        const UINT chunk =
            bytes_ - verifiedBytes_ > sizeof buffer ? sizeof buffer : UINT(bytes_ - verifiedBytes_);
        UINT count = 0;
        if (f_read(&file_, buffer, chunk, &count) != FR_OK || count != chunk) {
            fail(DataStatus::IoError);
            return status_;
        }
        verified_.update(buffer, count);
        verifiedBytes_ += count;
    }
    if (verifiedBytes_ != bytes_)
        return DataStatus::Busy;
    const auto closed = f_close(&file_);
    verifying_ = false;
    if (closed != FR_OK || verified_.value() != crc_.value() || f_rename(temporary_, final_) != FR_OK) {
        fail(DataStatus::IoError);
        return status_;
    }
    staged_ = false;
    status_ = DataStatus::Ok;
    return status_;
}
bool FatDataFile::commit() {
    DataStatus result;
    do {
        result = commitStep();
        Board::service();
    } while (result == DataStatus::Busy);
    return result == DataStatus::Ok;
}
} // namespace openosc
