#include "openosc/data/recorders.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/data/csv.hpp"
#include "openosc/platform/board.hpp"
#include <cstring>
#include <utility>

namespace openosc {
bool ScopeRecorder::begin(scope::FrameLease &&lease, Format format) {
    if (format != Format::Raw && format != Format::Csv) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    if (busy_ || !lease.valid() || !lease.view().valid())
        return false;
    if (format == Format::Raw && !stream_.begin(lease.view()))
        return false;
    const uint64_t reserve =
        format == Format::Raw ? stream_.size() : uint64_t(lease.view().metadata().samples) * 180 + 1024;
    if (!file_.begin(storage::Layout::Captures, "CAP", format == Format::Raw ? "RAW" : "CSV", reserve)) {
        status_ = file_.status();
        return false;
    }
    if (format == Format::Csv) {
        const auto bytes =
            Csv::scopeMetadata(lease.view().metadata(), reinterpret_cast<char *>(buffer_), sizeof buffer_);
        if (!bytes) {
            file_.cancel();
            status_ = DataStatus::InvalidArgument;
            return false;
        }
        if (!file_.write(buffer_, bytes) || !file_.write(Csv::ScopeHeader, std::strlen(Csv::ScopeHeader))) {
            status_ = file_.status();
            return false;
        }
    }
    lease_ = std::move(lease);
    format_ = format;
    offset_ = 0;
    finishing_ = false;
    busy_ = true;
    status_ = DataStatus::Busy;
    return true;
}
void ScopeRecorder::service() {
    if (!busy_)
        return;
    if (finishing_) {
        const auto result = file_.commitStep();
        if (result == DataStatus::Busy)
            return;
        status_ = result;
        busy_ = false;
        lease_.release();
        return;
    }
    size_t bytes = 0;
    if (format_ == Format::Raw) {
        bytes = stream_.read(offset_, buffer_, sizeof buffer_);
        offset_ += bytes;
        finishing_ = offset_ == stream_.size();
    } else {
        while (offset_ < lease_.view().metadata().samples && sizeof buffer_ - bytes >= 180) {
            const size_t length = Csv::scope(
                lease_.view(), offset_, reinterpret_cast<char *>(buffer_ + bytes), sizeof buffer_ - bytes);
            if (!length) {
                status_ = DataStatus::InvalidArgument;
                cancel();
                return;
            }
            bytes += length;
            ++offset_;
        }
        finishing_ = offset_ == lease_.view().metadata().samples;
    }
    if (!bytes) {
        status_ = DataStatus::InvalidArgument;
        cancel();
        return;
    }
    if (!file_.write(buffer_, bytes)) {
        status_ = file_.status();
        cancel();
    }
}
void ScopeRecorder::cancel(bool keep) {
    file_.cancel(keep);
    lease_.release();
    busy_ = finishing_ = false;
    if (status_ == DataStatus::Busy)
        status_ = DataStatus::NoData;
}
bool DmmCsvLogger::begin(uint64_t maximum, HeldPolicy policy) {
    if (policy != HeldPolicy::Exclude && policy != HeldPolicy::IncludeWithFlag) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    if (active_ || maximum < std::strlen(Csv::DmmHeader) + 256)
        return false;
    if (!file_.begin(storage::Layout::Logs, "DMM", "CSV", maximum)) {
        status_ = file_.status();
        return false;
    }
    const size_t header = std::strlen(Csv::DmmHeader);
    if (!file_.write(Csv::DmmHeader, header)) {
        status_ = file_.status();
        return false;
    }
    maximum_ = maximum;
    bytes_ = header;
    heldPolicy_ = policy;
    syncedAt_ = Board::milliseconds();
    active_ = true;
    finishing_ = hasSequence_ = false;
    status_ = DataStatus::Ok;
    return true;
}
bool DmmCsvLogger::append(const dmm::Result &result, bool held) {
    if (!active_ || finishing_)
        return false;
    if (hasSequence_ && sequence_ == result.sequence)
        return true;
    if ((held || result.packetHold) && heldPolicy_ == HeldPolicy::Exclude)
        return true;
    char line[256];
    const size_t bytes = Csv::dmm(result, held, line, sizeof line);
    if (!bytes) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    if (bytes > maximum_ - bytes_) {
        finishing_ = true;
        status_ = DataStatus::Overflow;
        return false;
    }
    if (!file_.write(line, bytes)) {
        status_ = file_.status();
        cancel();
        return false;
    }
    bytes_ += bytes;
    sequence_ = result.sequence;
    hasSequence_ = true;
    const uint32_t now = Board::milliseconds();
    if (uint32_t(now - syncedAt_) >= SyncMs) {
        syncedAt_ = now;
        if (!file_.sync()) { status_ = file_.status(); cancel(); return false; }
    }
    return true;
}
void DmmCsvLogger::service() {
    if (!active_ || !finishing_)
        return;
    const auto result = file_.commitStep();
    if (result == DataStatus::Busy)
        return;
    if (result != DataStatus::Ok || status_ != DataStatus::Overflow)
        status_ = result;
    active_ = finishing_ = false;
}
void DmmCsvLogger::cancel(bool keep) {
    file_.cancel(keep);
    active_ = finishing_ = false;
}
} // namespace openosc
