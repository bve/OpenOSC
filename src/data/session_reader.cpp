#include "openosc/data/session_reader.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/storage/open_readers.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <strings.h>

namespace openosc::dmm {
bool SessionScan::fail(const char *message) {
    close();
    error_ = message;
    state_ = State::Failed;
    return false;
}
void SessionScan::close() {
    if (open_) {
        f_close(&file_);
        --storage::openReaders;
    }
    open_ = false;
    state_ = State::Idle;
}
bool SessionScan::open(const char *path) {
    close();
    info_ = SessionInfo{};
    position_ = passStart_ = lineStart_ = lineLength_ = 0;
    records_ = invalid_ = checkpointCount_ = 0;
    stride_ = 1;
    first_ = last_ = lastStart_ = windowEnd_ = 0;
    overlong_ = done_ = false;
    error_ = nullptr;
    trend_.begin(0, 1);
    if (!path || f_open(&file_, path, FA_READ) != FR_OK) return fail("Cannot open session file");
    open_ = true;
    ++storage::openReaders;
    size_ = uint32_t(f_size(&file_)); // FAT32: below 4 GiB.
    state_ = State::Indexing;
    return true;
}
bool SessionScan::window(uint64_t startUs, uint64_t endUs) {
    if (!open_ || (state_ != State::Ready && state_ != State::Windowing)) return false;
    uint32_t offset = 0; // Before the first checkpoint: the file's start (its comments parse as such).
    for (unsigned i = 0; i < checkpointCount_ && uint64_t(checkpoints_[i].startMs) * 1000 <= startUs; ++i)
        offset = checkpoints_[i].offset;
    if (f_lseek(&file_, offset) != FR_OK) return fail("Session file seek failed");
    position_ = passStart_ = lineStart_ = offset;
    lineLength_ = 0;
    overlong_ = done_ = false;
    lastStart_ = 0;
    trend_.begin(startUs, endUs);
    windowEnd_ = trend_.endUs();
    state_ = State::Windowing;
    return true;
}
unsigned SessionScan::progress() const {
    if (state_ == State::Ready) return 100;
    const uint32_t total = size_ > passStart_ ? size_ - passStart_ : 0;
    return total ? unsigned(uint64_t(position_ - passStart_) * 100 / total) : 100;
}
void SessionScan::finishPass() {
    if (state_ == State::Indexing && (!info_.magic || !info_.header || !info_.context.valid())) {
        fail("Not a session file");
        return;
    }
    state_ = State::Ready;
}
// Rows run forward in time (each record starts where or after the one before it started): a row that
// does not is corrupt and skipped, so the checkpoints stay ordered and a window pass can stop early.
void SessionScan::line(const char *text, size_t length, uint32_t offset) {
    LogRecord record;
    SessionInfo scratch;
    const bool indexing = state_ == State::Indexing;
    const auto kind = SessionParser::parse(text, length, indexing ? info_ : scratch, record);
    if (kind == SessionParser::Line::Invalid) { if (indexing) ++invalid_; return; }
    if (kind != SessionParser::Line::Record) return;
    constexpr uint64_t MaxDurationUs = 1000000ULL * LogSettings::Durations[LogSettings::DurationCount - 1];
    constexpr double MaxTrendValue = std::numeric_limits<float>::max();
    if (record.startUs < lastStart_ || record.endUs > MaxDurationUs ||
        record.minimum < -MaxTrendValue || record.maximum > MaxTrendValue) {
        if (indexing) ++invalid_;
        return;
    }
    lastStart_ = record.startUs;
    if (!indexing) {
        if (record.startUs >= windowEnd_) { done_ = true; return; }
        trend_.add(record);
        return;
    }
    if (!records_) first_ = record.startUs;
    if (record.endUs > last_) last_ = record.endUs;
    if (records_ % stride_ == 0) {
        if (checkpointCount_ == Checkpoints) { // Every other one kept: twice the records between them.
            for (unsigned i = 0; i < Checkpoints / 2; ++i) checkpoints_[i] = checkpoints_[2 * i];
            checkpointCount_ = Checkpoints / 2;
            stride_ *= 2;
        }
        if (records_ % stride_ == 0) checkpoints_[checkpointCount_++] = {uint32_t(record.startUs / 1000), offset};
    }
    ++records_;
}
void SessionScan::step(uint32_t budget) {
    while (busy() && budget) {
        if (done_ || position_ >= size_) {
            if (!done_ && lineLength_ && !overlong_) line(lineBuffer_, lineLength_, lineStart_); // A last line cut short (an unfinished file).
            lineLength_ = 0;
            finishPass();
            return;
        }
        const uint32_t want = std::min({size_ - position_, uint32_t(Chunk), budget});
        UINT count = 0;
        if (f_read(&file_, chunk_, want, &count) != FR_OK || count != want) { fail("Session file read failed"); return; }
        for (UINT i = 0; i < count && !done_; ++i) {
            const char c = chunk_[i];
            if (c == '\n') {
                if (overlong_) { if (state_ == State::Indexing) ++invalid_; }
                else line(lineBuffer_, lineLength_, lineStart_);
                lineLength_ = 0;
                overlong_ = false;
                lineStart_ = position_ + i + 1;
            } else if (lineLength_ < sizeof lineBuffer_ - 1) lineBuffer_[lineLength_++] = c;
            else overlong_ = true;
        }
        position_ += count;
        budget = budget > count ? budget - count : 0;
    }
}

bool SessionCatalog::parseName(const char *name, uint32_t &number, bool &temporary) {
    if (!name || std::strlen(name) != 12 || strncasecmp(name, "SES", 3) || name[8] != '.') return false;
    uint32_t n = 0;
    for (unsigned i = 3; i < 8; ++i) {
        if (name[i] < '0' || name[i] > '9') return false;
        n = n * 10 + unsigned(name[i] - '0');
    }
    if (!strcasecmp(name + 9, "CSV")) temporary = false;
    else if (!strcasecmp(name + 9, "TMP")) temporary = true;
    else return false;
    if (!n) return false;
    number = n;
    return true;
}
bool SessionCatalog::collect(Side side, uint32_t limit) {
    uint32_t keys[PageSize];
    uint32_t sizes[PageSize];
    unsigned found = 0, beyond = 0; // beyond: keys at or above the limit (Below), above it (Above).
    count_ = 0;
    const auto opened = f_opendir(&directory_, storage::Layout::Logs);
    if (opened == FR_NO_PATH) { visible_ = first_ = selected_ = 0; return true; }
    if (opened != FR_OK) return fail("Cannot read the LOGS directory");
    FRESULT result;
    while ((result = f_readdir(&directory_, &found_)) == FR_OK && found_.fname[0]) {
        uint32_t number;
        bool temporary;
        if ((found_.fattrib & (AM_DIR | AM_HID | AM_SYS)) || !parseName(found_.fname, number, temporary)) continue;
        if (temporary && number == excluded_) continue; // Being written.
        const uint32_t key = SessionCatalog::key(number, temporary);
        ++count_;
        const bool wanted = side == Side::Below ? key < limit : key > limit;
        if (wanted == (side == Side::Above)) ++beyond;
        if (!wanted) continue;
        // Insertion into the page's order: descending below the limit, ascending above it.
        unsigned at = found;
        while (at && (side == Side::Below ? keys[at - 1] < key : keys[at - 1] > key)) --at;
        if (at >= PageSize) continue;
        const unsigned last = found < PageSize ? found : PageSize - 1;
        for (unsigned i = last; i > at; --i) { keys[i] = keys[i - 1]; sizes[i] = sizes[i - 1]; }
        keys[at] = key;
        sizes[at] = uint32_t(found_.fsize);
        if (found < PageSize) ++found;
    }
    const auto closed = f_closedir(&directory_);
    if (result != FR_OK || closed != FR_OK) return fail("LOGS directory read failed");
    visible_ = found;
    for (unsigned i = 0; i < found; ++i) {
        const unsigned from = side == Side::Below ? i : found - 1 - i; // Newest first on the page.
        auto &entry = entries_[i];
        entry = Entry{};
        entry.number = keys[from] / 2;
        entry.temporary = keys[from] & 1;
        entry.bytes = sizes[from];
        std::snprintf(entry.name, sizeof entry.name, "SES%05u.%s", unsigned(entry.number % 100000), entry.temporary ? "TMP" : "CSV");
    }
    first_ = side == Side::Below ? beyond : beyond - found; // Above: the keys above the limit end with this page.
    for (unsigned i = 0; i < found; ++i) describe(entries_[i]);
    return true;
}
// The header lines at the file's start, the trailer in its last bytes (a file without one is unfinished).
void SessionCatalog::describe(Entry &entry) {
    char path[PathBytes];
    std::snprintf(path, sizeof path, "%s/%s", storage::Layout::Logs, entry.name);
    if (f_open(&file_, path, FA_READ) != FR_OK) return;
    const uint32_t size = uint32_t(f_size(&file_));
    UINT count = 0;
    if (f_read(&file_, buffer_, sizeof buffer_, &count) == FR_OK) describeLines(entry, buffer_, count, false);
    if (size > sizeof buffer_ && f_lseek(&file_, size - 256) == FR_OK && f_read(&file_, buffer_, 256, &count) == FR_OK)
        describeLines(entry, buffer_, count, true);
    f_close(&file_);
    entry.described = entry.info.magic;
}
void SessionCatalog::describeLines(Entry &entry, const char *text, size_t length, bool skipFirst) {
    size_t start = 0;
    if (skipFirst) { // A line cut by the read's start.
        const char *newline = static_cast<const char *>(std::memchr(text, '\n', length));
        if (!newline) return;
        start = size_t(newline - text) + 1;
    }
    while (start < length) {
        const char *newline = static_cast<const char *>(std::memchr(text + start, '\n', length - start));
        if (!newline) return; // The rest is cut by the read's end.
        const size_t end = size_t(newline - text);
        if (text[start] == '#') {
            LogRecord unused;
            SessionParser::parse(text + start, end - start, entry.info, unused);
        }
        start = end + 1;
    }
}
bool SessionCatalog::refresh(uint32_t excluded) {
    excluded_ = excluded;
    error_ = nullptr;
    if (!fs_.mounted()) { count_ = visible_ = first_ = selected_ = 0; return fail("Storage not available"); }
    const uint32_t limit = first_ && visible_ ? key(entries_[0].number, entries_[0].temporary) + 1 : UINT32_MAX;
    if (!collect(Side::Below, limit)) return false;
    if (!visible_ && count_ && !collect(Side::Below, UINT32_MAX)) return false; // The page went: the newest again.
    if (!count_) { selected_ = first_ = 0; return true; }
    if (selected_ < first_) selected_ = first_;
    if (selected_ >= first_ + visible_) selected_ = first_ + visible_ - 1;
    return true;
}
bool SessionCatalog::move(int direction) {
    if (!count_ || !direction) return false;
    if (direction > 0) {
        if (selected_ + 1 < first_ + visible_) { ++selected_; return true; }
        if (first_ + visible_ >= count_ || !visible_) return false;
        const auto &last = entries_[visible_ - 1];
        if (!collect(Side::Below, key(last.number, last.temporary))) return false;
        selected_ = first_;
        return visible_ != 0;
    }
    if (selected_ > first_) { --selected_; return true; }
    if (!first_ || !visible_) return false;
    if (!collect(Side::Above, key(entries_[0].number, entries_[0].temporary))) return false;
    selected_ = first_ + (visible_ ? visible_ - 1 : 0);
    return visible_ != 0;
}
bool SessionCatalog::path(char *output, size_t capacity) const {
    const Entry *entry = current();
    if (!output || !capacity || !entry) return false;
    const int n = std::snprintf(output, capacity, "%s/%s", storage::Layout::Logs, entry->name);
    return n > 0 && size_t(n) < capacity;
}
bool SessionCatalog::eraseSelected() {
    char target[PathBytes];
    if (!fs_.mounted()) return fail("Storage not available");
    if (!path(target, sizeof target)) return false;
    if (storage::openReaders) return fail("A file is open over USB"); // FatFs does not lock: MMEM:DATA? may read it.
    if (f_unlink(target) != FR_OK) return fail("Cannot delete session");
    return refresh(excluded_);
}
} // namespace openosc::dmm
