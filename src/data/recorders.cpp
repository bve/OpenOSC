#include "openosc/data/recorders.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/data/csv.hpp"
#include "openosc/platform/board.hpp"
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

namespace openosc {
bool ScopeRecorder::begin(scope::FrameLease &&lease, Format format, scope::AcquisitionArena *arena, const Meter *meter,
                          const scope::TrendView *trend) {
    if (!supported(format)) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    if (busy_ || !lease.valid() || !lease.view().valid())
        return false;
    required_ = 0;
    format_ = format;
    if (raw(format) && !stream_.begin(lease.view())) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    meter_ = !raw(format) && meter ? meter->points : nullptr;
    trend_ = raw(format) ? nullptr : trend;
    trendMeter_ = trend_ && meter;
    meterCount_ = meter_ ? meter->count : 0;
    meterFunction_ = meter_ || trendMeter_ ? meter->function : "";
    meterQuantity_ = meter_ || trendMeter_ ? meter->quantity : Quantity::Unknown;
    meterNext_ = 0;
    meterValue_ = NAN;
    if (compressed(format)) return beginGzip(std::move(lease), arena);
    // Start immediately when even the largest representation fits. If the bound does not fit,
    // measure the actual record in slices before opening a temporary file.
    size_t header = 0;
    if (format == Format::Csv) {
        const auto &metadata = lease.view().metadata();
        const size_t row = trend_ ? Csv::trendRowLimit(metadata, trendMeter_) : Csv::scopeRowLimit(metadata, meter_ != nullptr);
        header = csvHeader(metadata);
        if (!row || !header) {
            status_ = DataStatus::InvalidArgument;
            return false;
        }
        required_ = header + std::strlen(columns()) + uint64_t(metadata.samples) * row;
    } else
        required_ = stream_.size();
    const bool opened = file_.begin(storage::Layout::Captures, "CAP", format == Format::Raw ? "RAW" : "CSV", required_);
    measuring_ = !opened && format == Format::Csv && file_.status() == DataStatus::NoSpace;
    if (!opened && !measuring_) {
        status_ = file_.status();
        return false;
    }
    if (measuring_) required_ = header + std::strlen(columns());
    else if (format == Format::Csv && !writeCsvHeader(header)) return false;
    lease_ = std::move(lease);
    offset_ = 0;
    finishing_ = false;
    busy_ = true;
    status_ = DataStatus::Busy;
    return true;
}
bool ScopeRecorder::beginGzip(scope::FrameLease &&lease, scope::AcquisitionArena *arena) {
    const bool csv = format_ == Format::CsvGz;
    size_t header = 0, names = 0;
    if (csv) {
        const auto &metadata = lease.view().metadata();
        header = csvHeader(metadata); // A trend's "# dmm:" and "# trend:" lines.
        names = std::strlen(columns());
        const size_t row = trend_ ? Csv::trendRowLimit(metadata, trendMeter_) : Csv::scopeRowLimit(metadata, meter_ != nullptr);
        if (!header || header + names > GzipStream::InputBytes || !row) {
            status_ = DataStatus::InvalidArgument;
            return false;
        }
    }
    if (!arena || !gzip_.begin(*arena)) { status_ = DataStatus::Busy; return false; } // A screenshot, a trial frame.
    if (csv) {
        std::memcpy(gzip_.input(), buffer_, header);
        std::memcpy(gzip_.input() + header, columns(), names);
    }
    // No upper bound is reserved: the free space is checked as each chunk is written.
    if (!file_.begin(storage::Layout::Captures, "CAP", csv ? "CSV.GZ" : "RAW.GZ", GzipStream::HeaderBytes)) {
        status_ = file_.status();
        gzip_.end();
        return false;
    }
    GzipStream::header(buffer_);
    out_ = GzipStream::HeaderBytes;
    pending_ = header + names; consumed_ = 0;
    lease_ = std::move(lease);
    offset_ = 0;
    finishing_ = measuring_ = false;
    busy_ = true;
    status_ = DataStatus::Busy;
    return true;
}
size_t ScopeRecorder::csvHeader(const scope::FrameMetadata &metadata) {
    size_t bytes = Csv::scopeMetadata(metadata, reinterpret_cast<char *>(buffer_), sizeof buffer_);
    if (bytes && (meter_ || trendMeter_)) {
        const size_t line = Csv::scopeMeter(meterFunction_, meterQuantity_, reinterpret_cast<char *>(buffer_ + bytes), sizeof buffer_ - bytes);
        bytes = line ? bytes + line : 0;
    }
    if (bytes && trend_) {
        const size_t line = Csv::scopeTrend(trend_->perPoint, reinterpret_cast<char *>(buffer_ + bytes), sizeof buffer_ - bytes);
        bytes = line ? bytes + line : 0;
    }
    return bytes;
}
const char *ScopeRecorder::columns() const {
    return trend_ ? trendMeter_ ? Csv::ScopeTrendMeterHeader : Csv::ScopeTrendHeader
                  : meter_ ? Csv::ScopeMeterHeader : Csv::ScopeHeader;
}
bool ScopeRecorder::writeCsvHeader(size_t bytes) {
    if (file_.write(buffer_, bytes) && file_.write(columns(), std::strlen(columns()))) return true;
    status_ = file_.status();
    if (status_ == DataStatus::NoSpace) file_.discard();
    return false;
}
const float *ScopeRecorder::meterField(size_t row) {
    if (!meter_) return nullptr;
    for (; meterNext_ < meterCount_; ++meterNext_) { // The reading this row holds.
        const MeterPoint &point = meter_[meterNext_];
        if (point.row > row) break;
        meterValue_ = point.value;
    }
    return &meterValue_;
}
size_t ScopeRecorder::csvRow(size_t row, char *output, size_t capacity) {
    if (trend_) return Csv::trend(lease_.view().metadata(), *trend_, row, trendMeter_, output, capacity);
    return Csv::scope(lease_.view(), row, output, capacity, meterField(row));
}
void ScopeRecorder::measureCsv() {
    constexpr unsigned RowsPerService = 96;
    const auto &frame = lease_.view();
    const auto &metadata = frame.metadata();
    for (unsigned i = 0; i < RowsPerService && offset_ < metadata.samples; ++i, ++offset_) {
        const size_t bytes = csvRow(offset_, reinterpret_cast<char *>(buffer_), sizeof buffer_);
        if (!bytes) { status_ = DataStatus::InvalidArgument; cancel(); return; }
        required_ += bytes;
    }
    if (offset_ < metadata.samples) return;
    const size_t header = csvHeader(metadata);
    if (!header) { status_ = DataStatus::InvalidArgument; cancel(); return; }
    if (!file_.begin(storage::Layout::Captures, "CAP", "CSV", required_)) {
        status_ = file_.status();
        cancel();
        return;
    }
    if (!writeCsvHeader(header)) { cancel(); return; }
    offset_ = 0;
    meterNext_ = 0; // The write goes over the readings again from row 0.
    meterValue_ = NAN;
    measuring_ = false;
}
size_t ScopeRecorder::csvRows(uint8_t *out, size_t capacity) {
    size_t bytes = 0;
    while (offset_ < lease_.view().metadata().samples && capacity - bytes >= 180) {
        const size_t length = csvRow(offset_, reinterpret_cast<char *>(out + bytes), capacity - bytes);
        if (!length) return 0;
        bytes += length;
        ++offset_;
    }
    return bytes;
}
size_t ScopeRecorder::rawBytes(uint8_t *out, size_t capacity) {
    const size_t bytes = stream_.read(offset_, out, capacity);
    offset_ += bytes;
    return bytes;
}
void ScopeRecorder::failGzip(DataStatus status) {
    status_ = status;
    cancel();
}
bool ScopeRecorder::flushGzip() {
    if (!out_) return true;
    if (!file_.writeWithinHeadroom(buffer_, out_)) {
        failGzip(file_.status());
        return false;
    }
    out_ = 0;
    return true;
}
// One input slice and at most one output chunk per pass.
void ScopeRecorder::serviceGzip() {
    const bool raw = format_ == Format::RawGz;
    const size_t end = raw ? stream_.size() : lease_.view().metadata().samples;
    if (pending_ == consumed_ && offset_ < end) {
        pending_ = raw ? rawBytes(gzip_.input(), GzipStream::InputBytes) : csvRows(gzip_.input(), GzipStream::InputBytes);
        consumed_ = 0;
        if (!pending_) { failGzip(DataStatus::InvalidArgument); return; }
    }
    size_t consumed = 0, produced = 0;
    if (!gzip_.compress(gzip_.input() + consumed_, pending_ - consumed_, offset_ == end, buffer_ + out_, sizeof buffer_ - out_, consumed, produced)) {
        failGzip(DataStatus::IoError);
        return;
    }
    consumed_ += consumed; out_ += produced;
    const bool complete = gzip_.complete();
    if (complete) gzip_.end(); // The Trial area back before the file's verification.
    if ((out_ == sizeof buffer_ || complete) && !flushGzip()) return;
    finishing_ = complete;
}
void ScopeRecorder::service() {
    if (!busy_)
        return;
    if (measuring_) { measureCsv(); return; }
    if (finishing_) {
        const auto result = file_.commitStep();
        if (result == DataStatus::Busy)
            return;
        status_ = result;
        if (compressed(format_) && result != DataStatus::Ok) file_.discard();
        busy_ = false;
        lease_.release();
        return;
    }
    if (compressed(format_)) { serviceGzip(); return; }
    size_t bytes = 0;
    if (format_ == Format::Raw) {
        bytes = rawBytes(buffer_, sizeof buffer_);
        finishing_ = offset_ == stream_.size();
    } else {
        bytes = csvRows(buffer_, sizeof buffer_); // None: a row that cannot be written.
        finishing_ = offset_ == lease_.view().metadata().samples;
    }
    if (!bytes) {
        status_ = DataStatus::InvalidArgument;
        cancel();
        return;
    }
    if (!file_.write(buffer_, bytes)) {
        status_ = file_.status();
        if (status_ == DataStatus::NoSpace) file_.discard();
        cancel();
    }
}
void ScopeRecorder::cancel(bool keep) {
    if (compressed(format_)) file_.discard();
    else file_.cancel(keep);
    gzip_.end();
    lease_.release();
    busy_ = finishing_ = measuring_ = false;
    pending_ = consumed_ = out_ = 0;
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

bool DmmSessionRecorder::begin(const dmm::LogSettings &settings, const dmm::Result &latest, dmm::Function function, uint64_t nowUs,
                               const char *board) {
    if (active_) return false;
    dmm::LogContext context;
    if (!settings.valid() || !settings.fits()) { status_ = DataStatus::InvalidArgument; return false; }
    if (!latest.sequence || !dmm::LogContext::of(latest, function, context)) { status_ = DataStatus::NoData; return false; }
    char line[dmm::SessionCsv::LineBytes * 2];
    const size_t header = dmm::SessionCsv::metadata(settings, context, board, line, sizeof line);
    if (!header) { status_ = DataStatus::InvalidArgument; return false; }
    if (!file_.begin(storage::Layout::Logs, Prefix, "CSV", settings.reserveBytes())) { status_ = file_.status(); return false; }
    if (!file_.write(line, header)) { status_ = file_.status(); return false; }
    if (!log_.begin(settings, context, nowUs)) { file_.cancel(); status_ = DataStatus::InvalidArgument; return false; }
    bytes_ = header;
    syncedAt_ = Board::milliseconds();
    active_ = true;
    finishing_ = false;
    status_ = DataStatus::Busy;
    return true;
}
bool DmmSessionRecorder::write(const char *line, size_t bytes) {
    if (!file_.write(line, bytes)) { status_ = file_.status(); active_ = finishing_ = false; return false; }
    bytes_ += bytes;
    return true;
}
// The trailer's room stays free: a record that would leave less ends the session as storage_full.
bool DmmSessionRecorder::record(const dmm::LogRecord &record) {
    char line[dmm::SessionCsv::LineBytes];
    const size_t bytes = dmm::SessionCsv::row(record, line, sizeof line);
    if (!bytes) { log_.abort(dmm::LogEnd::Error); return false; }
    if (bytes_ + bytes + dmm::SessionCsv::LineBytes > dmm::LogSettings::MaximumBytes) { log_.abort(dmm::LogEnd::StorageFull); return false; }
    return write(line, bytes);
}
void DmmSessionRecorder::settle() {
    if (!active_ || finishing_ || log_.active()) return;
    char line[dmm::SessionCsv::LineBytes];
    const size_t bytes = dmm::SessionCsv::trailer(log_.summary(), line, sizeof line);
    if (bytes && !write(line, bytes)) return;
    finishing_ = true;
}
void DmmSessionRecorder::observe(const dmm::Result &result, dmm::Function function) {
    if (!recording()) return;
    log_.observe(result, function, *this);
    settle();
}
void DmmSessionRecorder::stop(uint64_t nowUs, dmm::LogEnd end) {
    if (!recording()) return;
    log_.finish(nowUs, end, *this);
    settle();
}
void DmmSessionRecorder::service(uint64_t nowUs) {
    if (!active_) return;
    if (!finishing_) {
        log_.service(nowUs, *this);
        settle();
        if (!active_) return;
        const uint32_t now = Board::milliseconds();
        if (!finishing_ && uint32_t(now - syncedAt_) >= SyncMs) {
            syncedAt_ = now;
            if (!file_.sync()) { status_ = file_.status(); active_ = false; return; }
        }
        if (!finishing_) return;
    }
    const auto result = file_.commitStep();
    if (result == DataStatus::Busy) return;
    status_ = result;
    active_ = finishing_ = false;
}
void DmmSessionRecorder::cancel(bool keep) {
    if (!active_) return;
    file_.cancel(keep);
    if (log_.active()) log_.abort(dmm::LogEnd::Error);
    active_ = finishing_ = false;
    if (status_ == DataStatus::Busy) status_ = DataStatus::NoData;
}
#ifdef OPENOSC_APPLICATION
namespace {
alignas(8) __attribute__((section(".dmm_log"))) unsigned char sessionStorage[sizeof(DmmSessionRecorder)];
DmmSessionRecorder *session = nullptr;
} // namespace
DmmSessionRecorder &dmmSessionRecorder(Filesystem &fs) {
    if (!session) session = new (sessionStorage) DmmSessionRecorder(fs);
    return *session;
}
#endif
} // namespace openosc
