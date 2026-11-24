#pragma once
#include "openosc/acquisition/mailbox.hpp"
#include "openosc/acquisition/trend.hpp"
#include "openosc/data/fat_file.hpp"
#include "openosc/data/gzip_stream.hpp"
#include "openosc/data/raw_capture.hpp"
#include "openosc/data/session_log.hpp"
#include "openosc/measurement/dmm/result.hpp"

namespace openosc {
// Waveform exports into /openosc/CAPTURES. CSV.gz and RAW.gz (GitHub #166) wrap the original
// export bytes in a gzip member using the Trial area. Each write checks the remaining space.
// A failed or cancelled gzip export discards its temporary file. The Trial area is released
// once compression ends or the save is cancelled.
class ScopeRecorder {
public:
    enum class Format { Raw, Csv, CsvGz, RawGz };
    static constexpr bool raw(Format format) { return format == Format::Raw || format == Format::RawGz; }
    static constexpr bool compressed(Format format) { return format == Format::CsvGz || format == Format::RawGz; }
    static constexpr bool supported(Format format) { return raw(format) || format == Format::Csv || format == Format::CsvGz; }
    // A CSV's and CSV.gz's dmm column (Csv::ScopeMeterHeader): the DMM readings over its record in time
    // order, each from its first row on (a reading before the record from row 0), copied for the write's
    // whole length (DataAccess::meterColumn, into memory of its own); NaN leaves the field empty, as do
    // the rows before the first. A RAW save has none.
    struct MeterPoint {
        uint32_t row;
        float value;
    };
    struct Meter {
        const MeterPoint *points = nullptr;
        uint32_t count = 0;
        const char *function = "";
        Quantity quantity = Quantity::Unknown;
    };
    explicit ScopeRecorder(Filesystem &fs) : file_(fs) {}
    // trend: the copy of the trend on show (a CSV or a CSV.gz): its own points as rows of min, mean and max
    // instead of the frame's codes; its meter, if any, carries the dmm columns' header and no points (the
    // copy holds the DMM's, so a CSV.gz has them too).
    bool begin(scope::FrameLease &&lease, Format format, scope::AcquisitionArena *arena = nullptr, const Meter *meter = nullptr,
               const scope::TrendView *trend = nullptr);
    void service();
    void cancel(bool keep = false);
    bool busy() const { return busy_; }
    DataStatus status() const { return status_; }
    const char *path() const { return file_.path(); }
    // The reservation in bytes. When the CSV's upper bound did not fit, this is the exact byte count.
    // Streaming gzip exports report 0 here.
    uint64_t required() const { return required_; }
    Format format() const { return format_; } // The last begin()'s, also when it was refused.

private:
    bool writeCsvHeader(size_t bytes);
    void measureCsv();
    bool beginGzip(scope::FrameLease &&lease, scope::AcquisitionArena *arena);
    size_t csvRows(uint8_t *out, size_t capacity); // Rows from offset_ while 180 bytes are left.
    size_t rawBytes(uint8_t *out, size_t capacity);
    void serviceGzip();
    bool flushGzip(); // buffer_'s compressed bytes to the file.
    void failGzip(DataStatus status);
    // The CSV's metadata: the frame's, a trend copy's points, window and rate in a trend.
    scope::FrameMetadata csvMetadata(const scope::FrameMetadata &frame) const;
    // The CSV's metadata lines into buffer_, the "# dmm:" line with a dmm column and a trend's "# trend:" line: their
    // bytes, 0 when they do not fit.
    size_t csvHeader(const scope::FrameMetadata &metadata);
    // The column header: a trend's (Csv::ScopeTrendHeader, ScopeTrendMeterHeader with the dmm columns), else
    // Csv::ScopeMeterHeader with a dmm column, else ScopeHeader.
    const char *columns() const;
    const float *meterField(size_t row); // The dmm field of `row` (rows in order), nullptr without the column.
    size_t csvRow(size_t row, char *output, size_t capacity); // Csv::trend's row of a trend, else Csv::scope's.
    FatDataFile file_;
    GzipStream gzip_;
    size_t pending_ = 0, consumed_ = 0, out_ = 0; // Gzip input in the Trial area and output in buffer_.
    scope::FrameLease lease_;
    RawCaptureStream stream_;
    Format format_ = Format::Raw;
    size_t offset_ = 0;
    uint64_t required_ = 0;
    const MeterPoint *meter_ = nullptr;    // Meter::points, nullptr: no dmm column.
    const scope::TrendView *trend_ = nullptr;
    bool trendMeter_ = false; // A trend's rows carry the dmm columns.
    uint32_t rows_ = 0;       // A CSV's rows: the frame's points, a trend copy's own.
    uint32_t meterCount_ = 0, meterNext_ = 0; // The points; the next to come into force.
    float meterValue_ = 0;                  // The one in force (NaN: none).
    const char *meterFunction_ = "";        // Meter::function and quantity: the "# dmm:" line.
    Quantity meterQuantity_ = Quantity::Unknown;
    bool busy_ = false, finishing_ = false, measuring_ = false;
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
// A DMM session (GitHub #144, docs/DMM_SESSION_LOGGING.md) into /openosc/LOGS/SESnnnnn.CSV, beside the
// per-packet DMMnnnnn.CSV of DmmCsvLogger (unchanged; both may run at once). dmm::SessionLog decides
// the records; this writes them through FatDataFile: a .TMP synced every SyncMs, the trailer once the
// session ends (stop, its duration, the size limit), then the verified commit (service) renames it.
// An I/O error keeps the .TMP with what was written; so does a cancel(true) at shutdown.
class DmmSessionRecorder final : private dmm::SessionLog::Sink {
public:
    static constexpr const char *Prefix = "SES";
    static constexpr uint32_t SyncMs = 60000; // As DmmCsvLogger: each sync erases the directory and FAT blocks again.
    explicit DmmSessionRecorder(Filesystem &fs) : file_(fs) {}
    // A session of `settings` measuring what `latest` (of `function`) measures, from nowUs. False with
    // nothing started: invalid or unfitting settings (InvalidArgument), no reading of a known function
    // (NoData), the volume (the file's status: NoSpace, IoError).
    bool begin(const dmm::LogSettings &settings, const dmm::Result &latest, dmm::Function function, uint64_t nowUs, const char *board);
    void observe(const dmm::Result &result, dmm::Function function);
    void service(uint64_t nowUs); // Time (intervals, silence, the duration), the periodic sync, the commit.
    void stop(uint64_t nowUs, dmm::LogEnd end); // The session ends now; its file is committed by service().
    void cancel(bool keep = false);
    bool active() const { return active_; }               // Recording or committing.
    bool recording() const { return active_ && !finishing_; }
    DataStatus status() const { return status_; }         // Busy while active; then Ok or why it failed.
    dmm::LogEnd end() const { return log_.end(); }
    const char *path() const { return file_.path(); }     // The final name (.CSV).
    const dmm::SessionLog &log() const { return log_; }
    uint64_t bytes() const { return bytes_; }

private:
    bool record(const dmm::LogRecord &record) override;
    void settle(); // After the log acted: a failed write ends the recorder, an ended log writes its trailer.
    bool write(const char *line, size_t bytes);
    FatDataFile file_;
    dmm::SessionLog log_;
    uint64_t bytes_ = 0;
    uint32_t syncedAt_ = 0;
    bool active_ = false, finishing_ = false;
    DataStatus status_ = DataStatus::NoData;
};
#ifdef OPENOSC_APPLICATION
// The application's one recorder: ITCM (.dmm_log, zeroed at reset), built in place on the first call,
// so nothing of it is in DTCM beside main()'s frame (the ZT706S's stack margin).
DmmSessionRecorder &dmmSessionRecorder(Filesystem &fs);
#endif
} // namespace openosc
