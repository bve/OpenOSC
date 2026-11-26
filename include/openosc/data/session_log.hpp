#pragma once
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/log_settings.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::dmm {
// What a reading is to a session (docs/DMM_SESSION_LOGGING.md). Ok: a valid number of the session's
// function and quantity. The others carry no value: Overload (OL), NoReading (blank, dashes, an
// unreadable packet), Held (the DMM's own HOLD: a frozen source), OtherFunction (another function or
// quantity, or the DMM's own REL changed), NoData (no packet at all).
enum class LogStatus : uint8_t { Ok, Overload, NoReading, Held, OtherFunction, NoData, Count };
// Why an event record opened: the session's first record, a reading outside the band of the record
// before it, the first value after a record without values, a record without values.
enum class LogTrigger : uint8_t { None, Start, Deviation, Resume, State, Count };
// How a session ended.
enum class LogEnd : uint8_t { None, Manual, Duration, ModeChange, Shutdown, StorageFull, Error, Count };
const char *logStatusKey(LogStatus status);   // "ok", "overload", ...
const char *logTriggerKey(LogTrigger trigger); // "", "start", "deviation", ...
const char *logEndKey(LogEnd end);             // "", "manual", "duration", ...
bool logStatusFromKey(const char *text, size_t length, LogStatus &status);
bool logTriggerFromKey(const char *text, size_t length, LogTrigger &trigger);
bool logEndFromKey(const char *text, size_t length, LogEnd &end);

// What the session measures, fixed when it starts: the function, its quantity, and the DMM's own REL
// (its values are differences). The local HOLD and REL only change what the screen shows: the log keeps
// the live readings, never the held or offset ones.
// One measurement: V and mV of one coupling are one (an SI voltage either way), as A and mA; a reading
// whose function is not recognised (OL or blanks without a unit) belongs to it by its packet's function
// byte and current jack.
struct LogContext {
    Function function = Function::Unknown; // As the session started (its file says it).
    Quantity quantity = Quantity::Unknown;
    Kind kind = Kind::Unknown;
    bool current = false, peerRelative = false;
    bool valid() const { return function != Function::Unknown && quantity != Quantity::Unknown; }
    static Quantity quantityOf(Function function);
    static Function family(Function function); // mV -> V, mA -> A (each coupling kept).
    // The reading's function decides (Type K: the caller's Temperature); false without one.
    static bool of(const Result &result, Function function, LogContext &out);
    bool same(const Result &result, Function function) const;
    LogStatus classify(const Result &result, Function function) const;
    static const char *siUnit(Quantity quantity); // "V", "A", "Ohm", "F", "Hz", "degC".
};
// A row of the session file. Times are microseconds since the session start.
struct LogRecord {
    uint32_t index = 0;          // Interval: the interval's number (a no_data run: its first); event: its order.
    uint64_t startUs = 0, endUs = 0;
    LogStatus status = LogStatus::NoData;
    LogTrigger trigger = LogTrigger::None;
    uint32_t readings = 0, overloads = 0; // Ok readings; OL readings seen in it.
    double first = 0, last = 0, minimum = 0, maximum = 0, average = 0; // Only with readings.
};
// Totals over the whole session: its trailer line.
struct LogSummary {
    LogEnd end = LogEnd::None;
    uint64_t elapsedUs = 0;
    uint32_t records = 0, readings = 0, deviations = 0, overloads = 0;
    double minimum = 0, maximum = 0, average = 0;
};
// The readings of one record: first, last, min, max and a compensated sum (Neumaier) for the average,
// so a day of readings keeps the display's digits. Which statuses occurred, for a record without values.
class LogAccumulator {
public:
    void reset() { *this = LogAccumulator{}; }
    void add(double value);
    void note(LogStatus status) { seen_ |= uint8_t(1U << unsigned(status)); if (status == LogStatus::Overload) ++overloads_; }
    bool empty() const { return !seen_; } // Nothing at all, not even a reading without a value.
    uint32_t readings() const { return readings_; }
    uint32_t overloads() const { return overloads_; }
    // Ok with readings; else the first status seen in the order Overload, Held, NoReading,
    // OtherFunction; NoData when nothing was seen.
    LogStatus status() const;
    void fill(LogRecord &record) const;
    double sum() const { return sum_ + correction_; }
    double minimum() const { return minimum_; }
    double maximum() const { return maximum_; }

private:
    uint32_t readings_ = 0, overloads_ = 0;
    uint8_t seen_ = 0;
    double first_ = 0, last_ = 0, minimum_ = 0, maximum_ = 0, sum_ = 0, correction_ = 0;
};
// The session's state machine, no file: readings and time in, closed records out (Sink, in order).
// Interval: interval k is [k*I, (k+1)*I) after the start (the last one ends at the duration); it is
// written when the first reading or service() after it shows it has ended, as one row of what it
// received. An interval without any packet joins a run of such intervals, written as one no_data row
// once packets return or the session ends. Event: a record opens with the first reading; a valid reading
// whose distance from that record's first reading exceeds max(threshold x |first|, FloorCounts of the
// first reading's last digit) closes it and opens the next one with itself (trigger "deviation"). A
// reading without a value closes a value record and opens (or joins) a record of its status; the first
// valid reading after it opens a value record (trigger "resume"). No packet for SilenceUs opens a no_data
// record at the last packet + SilenceUs. A record ends where the next one starts, the last one where the
// session ends. Readings are never invented: an empty interval has no value, and times are the packets'.
class SessionLog {
public:
    class Sink {
    public:
        virtual bool record(const LogRecord &record) = 0; // False: the session ends (StorageFull/Error by the sink).
    protected:
        ~Sink() = default;
    };
    static constexpr uint64_t SilenceUs = 2000000;
    bool begin(const LogSettings &settings, const LogContext &context, uint64_t startUs);
    // A decoded reading at its own timestamp (the time it arrived). Older than the last one: ignored.
    void observe(const Result &result, Function function, Sink &sink);
    // Time passes: intervals end, a silent DMM, the duration. Ends the session at its duration.
    void service(uint64_t nowUs, Sink &sink);
    // Ends the session now (manual, mode change, shutdown): the open record is written up to nowUs.
    void finish(uint64_t nowUs, LogEnd end, Sink &sink);
    void abort(LogEnd end) { end_ = end; active_ = false; } // A sink failure: nothing more is written.
    bool active() const { return active_; }
    LogEnd end() const { return end_; }
    const LogSettings &settings() const { return settings_; }
    const LogContext &context() const { return context_; }
    uint64_t startUs() const { return startUs_; }
    uint64_t elapsedUs(uint64_t nowUs) const { return nowUs > startUs_ ? nowUs - startUs_ : 0; }
    LogSummary summary() const;

private:
    void emit(LogRecord &record, Sink &sink);
    // Interval mode.
    void advance(uint64_t timeUs, Sink &sink); // Every interval that ended by timeUs is written.
    void closeInterval(uint64_t endUs, Sink &sink);
    void flushRun(Sink &sink);
    uint64_t intervalUs() const { return uint64_t(settings_.intervalS()) * 1000000; }
    uint64_t durationUs() const { return uint64_t(settings_.durationS()) * 1000000; }
    // Event mode.
    void openEvent(LogStatus status, LogTrigger trigger, uint64_t atUs, const Result *reading, double value);
    void closeEvent(uint64_t endUs, Sink &sink);
    bool within(double value) const;
    LogSettings settings_;
    LogContext context_;
    uint64_t startUs_ = 0, lastUs_ = 0;   // Absolute; lastUs_: the newest packet.
    uint64_t recordStartUs_ = 0;          // Relative: the open record's (event) start.
    uint64_t runStartUs_ = 0, runEndUs_ = 0;
    uint32_t interval_ = 0, runIndex_ = 0, records_ = 0;
    LogAccumulator current_, total_;
    LogStatus openStatus_ = LogStatus::NoData;
    LogTrigger openTrigger_ = LogTrigger::None;
    double reference_ = 0, quantum_ = 0;
    uint32_t deviations_ = 0;
    bool active_ = false, open_ = false, run_ = false;
    LogEnd end_ = LogEnd::None;
};
// The session file's text (docs/DMM_SESSION_LOGGING.md), format "OpenOSC DMM session 1": "# " metadata
// lines, one header row, one row a record, a "# end=" trailer when the session closed. Numbers through
// Decimal (no %f): times in seconds with microseconds, values in SI with up to 12 significant digits.
// Rows without readings have empty value cells. Each writer returns its length, 0 when it does not fit.
class SessionCsv {
public:
    static constexpr const char *Magic = "# OpenOSC DMM session 1";
    static constexpr const char *Header = "index,start_s,end_s,status,trigger,readings,first,last,min,max,avg,overloads";
    static constexpr size_t LineBytes = 224; // Every line fits (the reader's buffer too).
    static size_t metadata(const LogSettings &settings, const LogContext &context, const char *board, char *output, size_t capacity);
    static size_t row(const LogRecord &record, char *output, size_t capacity);
    static size_t trailer(const LogSummary &summary, char *output, size_t capacity);
    // Seconds with up to six decimals, from microseconds ("12.5", "3600").
    static size_t seconds(uint64_t microseconds, char *output, size_t capacity);
};
// What a session file says about itself, as far as it was read.
struct SessionInfo {
    bool magic = false, header = false;
    LogSettings settings;
    LogContext context;
    bool ended = false; // A trailer was read: the session closed.
    LogSummary summary;
};
// One line of a session file at a time, never the whole file: comments into SessionInfo, rows into
// LogRecord. A line that does not parse is no record (the reader counts and skips it).
class SessionParser {
public:
    enum class Line : uint8_t { Empty, Comment, Header, Record, Invalid };
    static Line parse(const char *line, size_t length, SessionInfo &info, LogRecord &record);
    // Unsigned seconds with up to six decimals into microseconds, exactly (no double).
    static bool seconds(const char *text, size_t length, uint64_t &microseconds);
    static bool unsignedNumber(const char *text, size_t length, uint32_t &value);
    static bool number(const char *text, size_t length, double &value);

private:
    static void comment(const char *line, size_t length, SessionInfo &info);
    static bool record(const char *line, size_t length, LogRecord &record);
};
} // namespace openosc::dmm
