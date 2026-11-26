#include "openosc/data/session_log.hpp"
#include "openosc/data/csv.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/measurement/dmm/resolution.hpp"
#include "openosc/measurement/meter_function.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>

namespace openosc::dmm {
namespace {
constexpr const char *StatusKeys[] = {"ok", "overload", "no_reading", "held", "other_function", "no_data"};
constexpr const char *TriggerKeys[] = {"", "start", "deviation", "resume", "state"};
constexpr const char *EndKeys[] = {"", "manual", "duration", "mode_change", "shutdown", "storage_full", "error"};
static_assert(sizeof StatusKeys / sizeof *StatusKeys == unsigned(LogStatus::Count));
static_assert(sizeof TriggerKeys / sizeof *TriggerKeys == unsigned(LogTrigger::Count));
static_assert(sizeof EndKeys / sizeof *EndKeys == unsigned(LogEnd::Count));
template <class E, size_t N> bool fromKey(const char *const (&keys)[N], const char *text, size_t length, E &out) {
    for (size_t i = 0; i < N; ++i)
        if (std::strlen(keys[i]) == length && !std::strncmp(keys[i], text, length)) { out = E(i); return true; }
    return false;
}
// Appends to a line under construction; false (and the line unusable) once it does not fit.
class LineWriter {
public:
    LineWriter(char *output, size_t capacity) : out_(output), capacity_(capacity) { if (capacity_) out_[0] = 0; }
    bool text(const char *value) {
        const size_t n = std::strlen(value);
        if (!ok_ || n >= capacity_ - used_) return (ok_ = false);
        std::memcpy(out_ + used_, value, n + 1);
        used_ += n;
        return true;
    }
    bool character(char c) { const char s[2] = {c, 0}; return text(s); }
    bool unsignedNumber(uint64_t value) { char s[24]; return Decimal::integer(value, s, sizeof s) ? text(s) : (ok_ = false); }
    bool seconds(uint64_t us) { char s[32]; return SessionCsv::seconds(us, s, sizeof s) ? text(s) : (ok_ = false); }
    bool value(double v) { char s[40]; return Decimal::number(v, s, sizeof s, 12) ? text(s) : (ok_ = false); }
    size_t length() const { return ok_ && capacity_ ? used_ : 0; }

private:
    char *out_;
    size_t capacity_, used_ = 0;
    bool ok_ = true;
};
// The text between `begin` and the next comma (or the end), as [field, length); advances past it.
bool nextField(const char *&cursor, const char *end, const char *&field, size_t &length) {
    if (!cursor) return false;
    field = cursor;
    const char *comma = static_cast<const char *>(std::memchr(cursor, ',', size_t(end - cursor)));
    length = size_t((comma ? comma : end) - cursor);
    cursor = comma ? comma + 1 : nullptr;
    return true;
}
bool functionFromKey(const char *text, size_t length, Function &function) {
    for (unsigned i = 0; i < MeterFunctionCount; ++i)
        if (std::strlen(MeterFunctions[i].key) == length && !std::strncmp(MeterFunctions[i].key, text, length)) {
            function = Function(i);
            return true;
        }
    return false;
}
} // namespace

const char *logStatusKey(LogStatus s) { return unsigned(s) < unsigned(LogStatus::Count) ? StatusKeys[unsigned(s)] : ""; }
const char *logTriggerKey(LogTrigger t) { return unsigned(t) < unsigned(LogTrigger::Count) ? TriggerKeys[unsigned(t)] : ""; }
const char *logEndKey(LogEnd e) { return unsigned(e) < unsigned(LogEnd::Count) ? EndKeys[unsigned(e)] : ""; }
bool logStatusFromKey(const char *text, size_t length, LogStatus &status) { return fromKey(StatusKeys, text, length, status); }
bool logTriggerFromKey(const char *text, size_t length, LogTrigger &trigger) { return fromKey(TriggerKeys, text, length, trigger); }
bool logEndFromKey(const char *text, size_t length, LogEnd &end) { return length && fromKey(EndKeys, text, length, end); }

Quantity LogContext::quantityOf(Function f) {
    switch (f) {
    case Function::Temperature: return Quantity::Temperature;
    case Function::Diode: return Quantity::Voltage;
    case Function::Continuity: return Quantity::Resistance;
    default: return Functions::quantity(f);
    }
}
Function LogContext::family(Function f) {
    switch (f) {
    case Function::MillivoltsDc: return Function::VoltageDc;
    case Function::MillivoltsAc: return Function::VoltageAc;
    case Function::MilliampsDc: return Function::CurrentDc;
    case Function::MilliampsAc: return Function::CurrentAc;
    default: return f;
    }
}
bool LogContext::of(const Result &result, Function function, ThermocoupleType thermocouple, LogContext &out) {
    LogContext context;
    context.function = function;
    context.quantity = quantityOf(function);
    context.kind = functionKind(result.kind);
    context.current = result.currentInput;
    context.peerRelative = result.peerRelative;
    if (function == Function::Temperature) context.thermocouple = thermocouple;
    if (!context.valid()) return false;
    out = context;
    return true;
}
bool LogContext::same(const Result &r, Function f, ThermocoupleType t) const {
    if (function == Function::Temperature || f == Function::Temperature) return f == function && t == thermocouple;
    if (f != Function::Unknown) return family(f) == family(function);
    return kind != Kind::Unknown && functionKind(r.kind) == kind && r.currentInput == current;
}
LogStatus LogContext::classify(const Result &r, Function f, ThermocoupleType t) const {
    if (!same(r, f, t) || r.peerRelative != peerRelative) return LogStatus::OtherFunction;
    if (r.packetHold) return LogStatus::Held;
    if (r.primary.status == DataStatus::Overload) return LogStatus::Overload;
    if (r.primary.valid()) return r.primary.quantity == quantity ? LogStatus::Ok : LogStatus::OtherFunction;
    return LogStatus::NoReading;
}
const char *LogContext::siUnit(Quantity q) {
    static constexpr const char *units[] = {"", "V", "A", "Ohm", "F", "Hz", "degC"};
    return unsigned(q) < sizeof units / sizeof *units ? units[unsigned(q)] : "";
}

void LogAccumulator::add(double value) {
    if (!readings_) first_ = minimum_ = maximum_ = value;
    last_ = value;
    if (value < minimum_) minimum_ = value;
    if (value > maximum_) maximum_ = value;
    const double t = sum_ + value;
    correction_ += std::fabs(sum_) >= std::fabs(value) ? (sum_ - t) + value : (value - t) + sum_;
    sum_ = t;
    ++readings_;
    note(LogStatus::Ok);
}
LogStatus LogAccumulator::status() const {
    if (readings_) return LogStatus::Ok;
    for (auto s : {LogStatus::Overload, LogStatus::Held, LogStatus::NoReading, LogStatus::OtherFunction})
        if (seen_ & (1U << unsigned(s))) return s;
    return LogStatus::NoData;
}
void LogAccumulator::fill(LogRecord &r) const {
    r.status = status();
    r.readings = readings_;
    r.overloads = overloads_;
    if (!readings_) { r.first = r.last = r.minimum = r.maximum = r.average = 0; return; }
    r.first = first_; r.last = last_; r.minimum = minimum_; r.maximum = maximum_;
    r.average = sum() / readings_;
}

bool SessionLog::begin(const LogSettings &settings, const LogContext &context, uint64_t startUs) {
    if (active_ || !settings.valid() || !settings.fits() || !context.valid()) return false;
    current_.reset(); total_.reset(); // Field by field: no temporary of the whole log on the stack.
    recordStartUs_ = runStartUs_ = runEndUs_ = 0;
    interval_ = runIndex_ = records_ = deviations_ = 0;
    openStatus_ = LogStatus::NoData; openTrigger_ = LogTrigger::None;
    reference_ = quantum_ = 0;
    open_ = run_ = false;
    end_ = LogEnd::None;
    settings_ = settings;
    context_ = context;
    startUs_ = lastUs_ = startUs;
    active_ = true;
    return true;
}
void SessionLog::emit(LogRecord &record, Sink &sink) {
    if (!active_) return;
    if (!sink.record(record)) { active_ = false; if (end_ == LogEnd::None) end_ = LogEnd::Error; return; }
    ++records_;
}
void SessionLog::observe(const Result &result, Function function, ThermocoupleType thermocouple, Sink &sink) {
    const uint64_t t = result.timestampUs;
    if (!active_ || t < lastUs_ || t < startUs_) return;
    service(t, sink);
    if (!active_) return;
    const LogStatus status = context_.classify(result, function, thermocouple);
    const double value = result.primary.si;
    lastUs_ = t;
    if (status == LogStatus::Ok) total_.add(value);
    else if (status == LogStatus::Overload) total_.note(status);
    const uint64_t at = t - startUs_;
    if (settings_.mode() == LogMode::Interval) {
        flushRun(sink); // A packet again: the run of empty intervals before this one is complete.
        if (!active_) return;
        if (status == LogStatus::Ok) current_.add(value);
        else current_.note(status);
        return;
    }
    if (open_ && openStatus_ == status && (status != LogStatus::Ok || within(value))) {
        if (status == LogStatus::Ok) current_.add(value);
        else current_.note(status);
        return;
    }
    const bool first = !open_ && !records_;
    LogTrigger trigger = first ? LogTrigger::Start : LogTrigger::State;
    if (!first && status == LogStatus::Ok) trigger = openStatus_ == LogStatus::Ok ? LogTrigger::Deviation : LogTrigger::Resume;
    if (open_) closeEvent(at, sink);
    if (active_) openEvent(status, trigger, at, &result, value);
}
void SessionLog::service(uint64_t nowUs, Sink &sink) {
    if (!active_ || nowUs < startUs_) return;
    const uint64_t at = nowUs - startUs_;
    if (at >= durationUs()) { finish(startUs_ + durationUs(), LogEnd::Duration, sink); return; }
    if (settings_.mode() == LogMode::Interval) { advance(nowUs, sink); return; }
    if (nowUs - lastUs_ < SilenceUs || (open_ && openStatus_ == LogStatus::NoData)) return;
    // Silent since the start: nothing was measured from it on. Else from 2 s after the last packet.
    const uint64_t silent = !open_ && !records_ ? 0 : lastUs_ - startUs_ + SilenceUs;
    const bool first = !open_ && !records_;
    if (open_) closeEvent(silent, sink);
    if (active_) openEvent(LogStatus::NoData, first ? LogTrigger::Start : LogTrigger::State, silent, nullptr, 0);
}
void SessionLog::finish(uint64_t nowUs, LogEnd end, Sink &sink) {
    if (!active_) return;
    uint64_t at = nowUs > startUs_ ? nowUs - startUs_ : 0;
    if (at > durationUs()) at = durationUs();
    if (settings_.mode() == LogMode::Interval) {
        advance(startUs_ + at, sink);
        // The interval in progress: written when it received anything; an empty part is no record
        // (its time is in the trailer's elapsed time).
        if (active_ && !current_.empty() && at > uint64_t(interval_) * intervalUs()) closeInterval(at, sink);
        flushRun(sink);
    } else if (open_) closeEvent(at, sink);
    lastUs_ = startUs_ + at;
    if (end_ == LogEnd::None) end_ = end;
    active_ = false;
}
LogSummary SessionLog::summary() const {
    LogSummary s;
    s.end = end_;
    s.elapsedUs = lastUs_ - startUs_;
    s.records = records_;
    s.readings = total_.readings();
    s.deviations = deviations_;
    s.overloads = total_.overloads();
    if (s.readings) { s.minimum = total_.minimum(); s.maximum = total_.maximum(); s.average = total_.sum() / s.readings; }
    return s;
}
// Interval k ends at min((k+1) I, duration); a jump over several empty intervals (no packet in any)
// joins them to the no_data run at once, whatever their number.
void SessionLog::advance(uint64_t timeUs, Sink &sink) {
    const uint64_t at = timeUs - startUs_;
    const uint64_t k = at / intervalUs();
    if (k <= interval_) return;
    const uint64_t end = uint64_t(interval_ + 1) * intervalUs();
    closeInterval(end < durationUs() ? end : durationUs(), sink);
    if (!active_) return;
    if (k > uint64_t(interval_) + 1) {
        if (!run_) { run_ = true; runIndex_ = interval_ + 1; runStartUs_ = uint64_t(interval_ + 1) * intervalUs(); }
        runEndUs_ = k * intervalUs();
    }
    interval_ = uint32_t(k);
    current_.reset();
}
void SessionLog::closeInterval(uint64_t endUs, Sink &sink) {
    const uint64_t start = uint64_t(interval_) * intervalUs();
    if (current_.empty()) {
        if (!run_) { run_ = true; runIndex_ = interval_; runStartUs_ = start; }
        runEndUs_ = endUs;
        return;
    }
    flushRun(sink);
    LogRecord record;
    record.index = interval_;
    record.startUs = start;
    record.endUs = endUs;
    current_.fill(record);
    emit(record, sink);
}
void SessionLog::flushRun(Sink &sink) {
    if (!run_) return;
    run_ = false;
    LogRecord record;
    record.index = runIndex_;
    record.startUs = runStartUs_;
    record.endUs = runEndUs_;
    record.status = LogStatus::NoData;
    emit(record, sink);
}
void SessionLog::openEvent(LogStatus status, LogTrigger trigger, uint64_t atUs, const Result *reading, double value) {
    open_ = true;
    openStatus_ = status;
    openTrigger_ = trigger;
    recordStartUs_ = atUs;
    current_.reset();
    if (status != LogStatus::Ok) { current_.note(status); return; }
    current_.add(value);
    reference_ = value;
    quantum_ = reading ? Resolution::of(*reading).quantum() : 0;
    if (trigger == LogTrigger::Deviation) ++deviations_;
}
void SessionLog::closeEvent(uint64_t endUs, Sink &sink) {
    if (!open_) return;
    open_ = false;
    LogRecord record;
    record.index = records_;
    record.startUs = recordStartUs_;
    record.endUs = endUs > recordStartUs_ ? endUs : recordStartUs_;
    record.trigger = openTrigger_;
    current_.fill(record);
    emit(record, sink);
}
// In counts of the reference's last digit, so the band's edge does not depend on binary fractions
// (1.234 - 1.229 is 5 counts of 1 mV, never 5.000000000000115). The floor keeps a reference at or near
// zero from opening a record on every count of noise.
bool SessionLog::within(double value) const {
    const double threshold = settings_.thresholdPermille() / 1000.0;
    if (quantum_ > 0) {
        const double counts = std::round((value - reference_) / quantum_);
        double band = threshold * std::fabs(reference_) / quantum_;
        if (band < LogSettings::FloorCounts) band = LogSettings::FloorCounts;
        return std::fabs(counts) <= band;
    }
    return std::fabs(value - reference_) <= threshold * std::fabs(reference_) * (1 + 1e-9);
}

size_t SessionCsv::seconds(uint64_t us, char *output, size_t capacity) {
    char whole[24];
    if (!output || !capacity || !Decimal::integer(us / 1000000, whole, sizeof whole)) return 0;
    uint32_t fraction = uint32_t(us % 1000000);
    char digits[8]{};
    unsigned count = 0;
    if (fraction) {
        for (int i = 5; i >= 0; --i) { digits[i] = char('0' + fraction % 10); fraction /= 10; }
        count = 6;
        while (count && digits[count - 1] == '0') --count;
        digits[count] = 0;
    }
    const int n = count ? std::snprintf(output, capacity, "%s.%s", whole, digits) : std::snprintf(output, capacity, "%s", whole);
    return n > 0 && size_t(n) < capacity ? size_t(n) : 0;
}
size_t SessionCsv::metadata(const LogSettings &s, const LogContext &c, const char *board, char *output, size_t capacity) {
    if (!output || !s.valid() || !c.valid() || unsigned(c.function) >= MeterFunctionCount) return 0;
    LineWriter w(output, capacity);
    w.text(Magic); w.text("\r\n# mode="); w.text(LogSettings::key(s.mode()));
    w.text(",interval_s="); w.unsignedNumber(s.intervalS());
    w.text(",duration_s="); w.unsignedNumber(s.durationS());
    w.text(",threshold_pct="); w.unsignedNumber(s.thresholdPermille() / 10); w.character('.'); w.unsignedNumber(s.thresholdPermille() % 10);
    w.text(",floor_counts="); w.unsignedNumber(LogSettings::FloorCounts);
    w.text("\r\n# function="); w.text(MeterFunctions[unsigned(c.function)].key);
    if (c.function == Function::Temperature && c.thermocouple != ThermocoupleType::Count) {
        w.text(",thermocouple="); w.text(Thermocouple::letter(c.thermocouple));
    }
    w.text(",quantity="); w.text(Csv::quantity(c.quantity));
    w.text(",unit="); w.text(LogContext::siUnit(c.quantity));
    w.text(",dmm_relative="); w.unsignedNumber(c.peerRelative);
    w.text(",board="); w.text(board ? board : "");
    w.text("\r\n# time=seconds since the session start (no real-time clock),values=SI units\r\n");
    w.text(Header); w.text("\r\n");
    return w.length();
}
size_t SessionCsv::row(const LogRecord &r, char *output, size_t capacity) {
    if (!output || unsigned(r.status) >= unsigned(LogStatus::Count) || unsigned(r.trigger) >= unsigned(LogTrigger::Count)) return 0;
    LineWriter w(output, capacity);
    w.unsignedNumber(r.index); w.character(',');
    w.seconds(r.startUs); w.character(',');
    w.seconds(r.endUs); w.character(',');
    w.text(logStatusKey(r.status)); w.character(',');
    w.text(logTriggerKey(r.trigger)); w.character(',');
    w.unsignedNumber(r.readings);
    for (double v : {r.first, r.last, r.minimum, r.maximum, r.average}) {
        w.character(',');
        if (r.readings) w.value(v);
    }
    w.character(','); w.unsignedNumber(r.overloads);
    w.text("\r\n");
    return w.length();
}
size_t SessionCsv::trailer(const LogSummary &s, char *output, size_t capacity) {
    if (!output || s.end == LogEnd::None || unsigned(s.end) >= unsigned(LogEnd::Count)) return 0;
    LineWriter w(output, capacity);
    w.text("# end="); w.text(logEndKey(s.end));
    w.text(",elapsed_s="); w.seconds(s.elapsedUs);
    w.text(",records="); w.unsignedNumber(s.records);
    w.text(",readings="); w.unsignedNumber(s.readings);
    w.text(",deviations="); w.unsignedNumber(s.deviations);
    w.text(",overloads="); w.unsignedNumber(s.overloads);
    for (const auto &[key, v] : {std::pair{",min=", s.minimum}, std::pair{",max=", s.maximum}, std::pair{",avg=", s.average}}) {
        w.text(key);
        if (s.readings) w.value(v);
    }
    w.text("\r\n");
    return w.length();
}

bool SessionParser::unsignedNumber(const char *text, size_t length, uint32_t &value) {
    if (!length || length > 10) return false;
    uint64_t v = 0;
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') return false;
        v = v * 10 + unsigned(text[i] - '0');
    }
    if (v > UINT32_MAX) return false;
    value = uint32_t(v);
    return true;
}
bool SessionParser::seconds(const char *text, size_t length, uint64_t &us) {
    if (!length) return false;
    uint64_t whole = 0, fraction = 0;
    size_t i = 0;
    unsigned digits = 0, decimals = 0;
    for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) {
        if (digits >= 12) return false; // Beyond any session.
        whole = whole * 10 + unsigned(text[i] - '0');
    }
    if (!digits) return false;
    if (i < length) {
        if (text[i++] != '.') return false;
        for (; i < length; ++i) {
            if (text[i] < '0' || text[i] > '9') return false;
            if (decimals < 6) { fraction = fraction * 10 + unsigned(text[i] - '0'); ++decimals; } // Finer than 1 us: dropped.
        }
    }
    for (; decimals < 6; ++decimals) fraction *= 10;
    us = whole * 1000000 + fraction;
    return true;
}
// [-]digits[.digits][e[+-]digits]: the mantissa's digits as an integer (19 at most kept, the rest
// only move the exponent), scaled by a power of ten once.
bool SessionParser::number(const char *text, size_t length, double &value) {
    size_t i = 0;
    bool negative = false;
    if (i < length && (text[i] == '-' || text[i] == '+')) negative = text[i++] == '-';
    uint64_t mantissa = 0;
    int exponent = 0, kept = 0, digits = 0;
    for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i, ++digits) {
        if (kept < 19) { mantissa = mantissa * 10 + unsigned(text[i] - '0'); if (mantissa) ++kept; }
        else ++exponent;
    }
    if (i < length && text[i] == '.') {
        for (++i; i < length && text[i] >= '0' && text[i] <= '9'; ++i, ++digits)
            if (kept < 19) { mantissa = mantissa * 10 + unsigned(text[i] - '0'); --exponent; if (mantissa) ++kept; }
    }
    if (!digits) return false;
    if (i < length && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        bool minus = false;
        if (i < length && (text[i] == '-' || text[i] == '+')) minus = text[i++] == '-';
        int e = 0, n = 0;
        for (; i < length && text[i] >= '0' && text[i] <= '9'; ++i, ++n) { if (e < 1000) e = e * 10 + (text[i] - '0'); }
        if (!n) return false;
        exponent += minus ? -e : e;
    }
    if (i != length || exponent > 308 || exponent < -330) return false;
    double v = double(mantissa);
    // Exact powers of ten up to 1e22: one multiplication or division for every reading.
    static constexpr double Powers[] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                        1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    for (int e = exponent; e; ) {
        const int step = e > 0 ? (e > 22 ? 22 : e) : (e < -22 ? -22 : e);
        v = step > 0 ? v * Powers[step] : v / Powers[-step];
        e -= step;
    }
    if (!std::isfinite(v)) return false;
    value = negative ? -v : v;
    return true;
}
void SessionParser::comment(const char *line, size_t length, SessionInfo &info) {
    const size_t magic = std::strlen(SessionCsv::Magic);
    if (length == magic && !std::strncmp(line, SessionCsv::Magic, magic)) { info.magic = true; return; }
    const char *cursor = line + 1, *end = line + length;
    while (cursor < end && *cursor == ' ') ++cursor;
    const char *field;
    size_t n;
    bool trailer = false;
    LogSummary summary;
    while (nextField(cursor, end, field, n)) {
        const char *equals = static_cast<const char *>(std::memchr(field, '=', n));
        if (!equals) continue;
        const size_t keyLength = size_t(equals - field), valueLength = n - keyLength - 1;
        const char *v = equals + 1;
        const auto is = [&](const char *key) { return std::strlen(key) == keyLength && !std::strncmp(field, key, keyLength); };
        uint32_t u = 0;
        double d = 0;
        if (is("mode")) {
            if (valueLength == 5 && !std::strncmp(v, "event", 5)) info.settings.mode(LogMode::Event);
            else if (valueLength == 8 && !std::strncmp(v, "interval", 8)) info.settings.mode(LogMode::Interval);
        } else if (is("interval_s") && unsignedNumber(v, valueLength, u)) info.settings.intervalS(u);
        else if (is("duration_s") && unsignedNumber(v, valueLength, u)) info.settings.durationS(u);
        else if (is("threshold_pct") && number(v, valueLength, d) && d >= 0 && d < 1e6) info.settings.thresholdPermille(uint32_t(std::lround(d * 10)));
        else if (is("function")) {
            Function f;
            if (functionFromKey(v, valueLength, f)) { info.context.function = f; info.context.quantity = LogContext::quantityOf(f); }
        } else if (is("dmm_relative") && unsignedNumber(v, valueLength, u)) info.context.peerRelative = u != 0;
        else if (is("end")) trailer = logEndFromKey(v, valueLength, summary.end);
        else if (is("elapsed_s")) seconds(v, valueLength, summary.elapsedUs);
        else if (is("records") && unsignedNumber(v, valueLength, u)) summary.records = u;
        else if (is("readings") && unsignedNumber(v, valueLength, u)) summary.readings = u;
        else if (is("deviations") && unsignedNumber(v, valueLength, u)) summary.deviations = u;
        else if (is("overloads") && unsignedNumber(v, valueLength, u)) summary.overloads = u;
        else if (is("min") && number(v, valueLength, d)) summary.minimum = d;
        else if (is("max") && number(v, valueLength, d)) summary.maximum = d;
        else if (is("avg") && number(v, valueLength, d)) summary.average = d;
    }
    if (trailer) { info.ended = true; info.summary = summary; }
}
bool SessionParser::record(const char *line, size_t length, LogRecord &r) {
    const char *cursor = line, *end = line + length, *field[12];
    size_t n[12];
    for (unsigned i = 0; i < 12; ++i)
        if (!nextField(cursor, end, field[i], n[i])) return false;
    if (cursor) return false; // More than twelve fields.
    LogRecord out;
    if (!unsignedNumber(field[0], n[0], out.index) || !seconds(field[1], n[1], out.startUs) || !seconds(field[2], n[2], out.endUs) ||
        out.endUs < out.startUs || !logStatusFromKey(field[3], n[3], out.status) || !logTriggerFromKey(field[4], n[4], out.trigger) ||
        !unsignedNumber(field[5], n[5], out.readings) || !unsignedNumber(field[11], n[11], out.overloads))
        return false;
    if ((out.status == LogStatus::Ok) != (out.readings != 0)) return false;
    double *values[] = {&out.first, &out.last, &out.minimum, &out.maximum, &out.average};
    for (unsigned i = 0; i < 5; ++i) {
        if (!out.readings) { if (n[6 + i]) return false; continue; }
        if (!number(field[6 + i], n[6 + i], *values[i])) return false;
    }
    if (out.readings && !(out.minimum <= out.maximum)) return false;
    r = out;
    return true;
}
SessionParser::Line SessionParser::parse(const char *line, size_t length, SessionInfo &info, LogRecord &r) {
    while (length && (line[length - 1] == '\r' || line[length - 1] == '\n')) --length;
    if (!length) return Line::Empty;
    if (line[0] == '#') { comment(line, length, info); return Line::Comment; }
    const size_t header = std::strlen(SessionCsv::Header);
    if (length == header && !std::strncmp(line, SessionCsv::Header, header)) { info.header = true; return Line::Header; }
    return record(line, length, r) ? Line::Record : Line::Invalid;
}
} // namespace openosc::dmm
