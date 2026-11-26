#pragma once
#include <cstdint>

namespace openosc::dmm {
// DMM session logging (GitHub #144, docs/DMM_SESSION_LOGGING.md): what the next session records.
// Interval: one row per interval with the count, first, last, min, max and average of the readings it
// received. Event: one record per stretch of readings within a band around the record's first reading
// (Fluke-style "event" records). The duration stops either mode; the size limit bounds both.
// Kept as four indices into the lists below in two bytes: MeterPreferences has exactly that room (the
// whole Preferences sits on several stacks and must not grow).
enum class LogMode : uint8_t { Interval, Event, Count };
struct LogSettings {
    // Seconds. The DMM sends a reading about every 330 ms: 1 s holds about three of them.
    static constexpr uint32_t Intervals[] = {1, 2, 5, 10, 30, 60, 120, 300, 600};
    static constexpr uint32_t Durations[] = {60, 300, 600, 1800, 3600, 7200, 14400, 28800, 43200, 86400, 172800, 604800};
    // The event band in per mille (tenths of a percent) of the record's first reading: 0.1 % .. 50 %.
    static constexpr uint32_t Bands[] = {1, 2, 5, 10, 20, 50, 100, 200, 500};
    static constexpr unsigned IntervalCount = sizeof Intervals / sizeof *Intervals;
    static constexpr unsigned DurationCount = sizeof Durations / sizeof *Durations;
    static constexpr unsigned BandCount = sizeof Bands / sizeof *Bands;
    static constexpr unsigned DefaultInterval = 0, DefaultDuration = 4, DefaultBand = 5; // 1 s, 1 h, 5.0 %.
    // The band's absolute floor, in units of the reference reading's last digit: near zero a
    // percentage alone is narrower than the display's own noise.
    static constexpr uint32_t FloorCounts = 5;
    // One session file at most (FatDataFile verifies it at commit; ShutdownSequence::FinishMs bounds
    // that read): a session that reaches it ends as "storage_full". An interval plan whose rows cannot
    // fit (RowBytes each, a conservative typical row) is refused before it starts.
    static constexpr uint64_t MaximumBytes = 4ULL * 1024 * 1024;
    static constexpr uint32_t RowBytes = 80;
    static constexpr uint32_t MaximumRows = uint32_t((MaximumBytes - 1024) / RowBytes);
    // The bytes the file is checked against at start (the free space): the interval plan's rows, or for
    // events their worst case, one record a reading (ReadingsPerSecond, EventBytes each); either at most
    // the limit.
    static constexpr uint32_t ReadingsPerSecond = 4, EventBytes = 110;

    LogSettings() : mode_(0), interval_(DefaultInterval), duration_(DefaultDuration), band_(DefaultBand), unused_(0) {}
    LogMode mode() const { return LogMode(mode_); }
    uint32_t intervalS() const { return interval_ < IntervalCount ? Intervals[interval_] : 0; }
    uint32_t durationS() const { return duration_ < DurationCount ? Durations[duration_] : 0; }
    uint32_t thresholdPermille() const { return band_ < BandCount ? Bands[band_] : 0; }
    unsigned intervalIndex() const { return interval_; }
    unsigned durationIndex() const { return duration_; }
    unsigned bandIndex() const { return band_; }
    // Each false (nothing changed) for a value outside its list.
    bool mode(LogMode mode) { if (unsigned(mode) >= unsigned(LogMode::Count)) return false; mode_ = uint16_t(mode) & 1; return true; }
    bool intervalS(uint32_t seconds) { return intervalIndex(index(Intervals, IntervalCount, seconds)); }
    bool durationS(uint32_t seconds) { return durationIndex(index(Durations, DurationCount, seconds)); }
    bool thresholdPermille(uint32_t permille) { return bandIndex(index(Bands, BandCount, permille)); }
    bool intervalIndex(unsigned i) { if (i >= IntervalCount) return false; interval_ = uint16_t(i) & 15; return true; }
    bool durationIndex(unsigned i) { if (i >= DurationCount) return false; duration_ = uint16_t(i) & 15; return true; }
    bool bandIndex(unsigned i) { if (i >= BandCount) return false; band_ = uint16_t(i) & 15; return true; }

    // Rows an interval plan writes at most (a partial last interval included).
    uint32_t plannedRows() const { return intervalS() ? (durationS() + intervalS() - 1) / intervalS() : 0; }
    uint64_t reserveBytes() const {
        const uint64_t rows = mode() == LogMode::Interval ? uint64_t(plannedRows()) * RowBytes
                                                          : uint64_t(durationS()) * ReadingsPerSecond * EventBytes;
        return rows + 1024 < MaximumBytes ? rows + 1024 : MaximumBytes;
    }
    bool fits() const { return mode() != LogMode::Interval || plannedRows() <= MaximumRows; }
    bool valid() const { return mode_ < unsigned(LogMode::Count) && interval_ < IntervalCount && duration_ < DurationCount && band_ < BandCount; }
    bool operator==(const LogSettings &b) const {
        return mode_ == b.mode_ && interval_ == b.interval_ && duration_ == b.duration_ && band_ == b.band_;
    }
    bool operator!=(const LogSettings &b) const { return !(*this == b); }
    static const char *key(LogMode mode) { return mode == LogMode::Event ? "event" : "interval"; } // settings file, CSV
    static const char *name(LogMode mode) { return mode == LogMode::Event ? "Event" : "Interval"; }

private:
    static unsigned index(const uint32_t *list, unsigned count, uint32_t value) {
        unsigned i = 0;
        while (i < count && list[i] != value) ++i;
        return i; // count: not in the list.
    }
    uint16_t mode_ : 1, interval_ : 4, duration_ : 4, band_ : 4, unused_ : 3;
};
static_assert(sizeof(LogSettings) == 2, "LogSettings fills MeterPreferences' two spare bytes");
} // namespace openosc::dmm
