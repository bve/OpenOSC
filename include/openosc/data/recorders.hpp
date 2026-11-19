#pragma once
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/data/fat_file.hpp"
#include "openosc/data/raw_capture.hpp"
#include "openosc/measurement/dmm/result.hpp"

namespace openosc {
class ScopeRecorder {
public:
    enum class Format { Raw, Csv };
    explicit ScopeRecorder(Filesystem &fs) : file_(fs) {}
    bool begin(scope::FrameLease &&lease, Format format);
    void service();
    void cancel(bool keep = false);
    bool busy() const { return busy_; }
    DataStatus status() const { return status_; }
    const char *path() const { return file_.path(); }

private:
    FatDataFile file_;
    scope::FrameLease lease_;
    RawCaptureStream stream_;
    Format format_ = Format::Raw;
    size_t offset_ = 0;
    bool busy_ = false, finishing_ = false;
    DataStatus status_ = DataStatus::NoData;
    uint8_t buffer_[4096]{};
};
class DmmCsvLogger {
public:
    enum class HeldPolicy { Exclude, IncludeWithFlag };
    explicit DmmCsvLogger(Filesystem &fs) : file_(fs) {}
    bool begin(uint64_t maximumBytes, HeldPolicy policy = HeldPolicy::Exclude);
    bool append(const dmm::Result &result, bool userHold);
    void finish() { finishing_ = active_; }
    void service();
    void cancel(bool keep = false);
    bool active() const { return active_; }
    DataStatus status() const { return status_; }
    const char *path() const { return file_.path(); }

private:
    FatDataFile file_;
    HeldPolicy heldPolicy_ = HeldPolicy::Exclude;
    uint64_t bytes_ = 0, maximum_ = 0;
    static constexpr uint32_t SyncMs = 60000; // Each sync erases the directory and FAT blocks again: NOR wear.
    uint32_t sequence_ = 0, syncedAt_ = 0;
    bool active_ = false, finishing_ = false, hasSequence_ = false;
    DataStatus status_ = DataStatus::NoData;
};
} // namespace openosc
