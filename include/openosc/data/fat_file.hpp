#pragma once
#include "openosc/data/status.hpp"
#include "openosc/storage/filesystem.hpp"
#include "openosc/update/crc32.hpp"

namespace openosc {
class FatDataFile {
public:
    static constexpr uint32_t FreeHeadroom = 8192;
    explicit FatDataFile(Filesystem &fs) : fs_(fs) {}
    ~FatDataFile();
    bool begin(const char *directory, const char *prefix, const char *extension, uint64_t reserveBytes);
    bool write(const void *data, size_t bytes);
    bool sync(); // The size so far reaches the directory: a power cut keeps the .TMP's data.
    bool commit();
    DataStatus commitStep();
    void cancel(bool keep = false); // keep: the .TMP stays (written data survives); else it is deleted.
    const char *path() const { return final_; }
    DataStatus status() const { return status_; }

private:
    bool fail(DataStatus status);
    Filesystem &fs_;
    FIL file_{};
    bool open_ = false, verifying_ = false;
    uint64_t bytes_ = 0, verifiedBytes_ = 0;
    Crc32 crc_, verified_;
    char final_[96]{}, temporary_[96]{};
    DataStatus status_ = DataStatus::NoData;
};
} // namespace openosc
