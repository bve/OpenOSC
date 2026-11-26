#pragma once
#include "openosc/data/session_log.hpp"
#include "openosc/data/trend_window.hpp"
#include "openosc/storage/filesystem.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::dmm {
// A session file read in bounded slices (step), never whole: first an index pass (what the file says,
// its extent, records and invalid lines, a sparse table of offsets by time), then window passes that
// seek through that table and reduce the window's records into a TrendWindow. Every slice reads and
// parses at most the bytes it is given, so a large file on slow storage leaves each main-loop pass short.
// A line that does not parse is counted and skipped; a read error fails the scan (error()).
class SessionScan {
public:
    enum class State : uint8_t { Idle, Indexing, Windowing, Ready, Failed };
    static constexpr unsigned Checkpoints = 128, Chunk = 512;
    ~SessionScan() { close(); }
    bool open(const char *path); // Starts the index pass; false (error()) when the file cannot be opened.
    void close();
    void step(uint32_t budgetBytes);
    bool window(uint64_t startUs, uint64_t endUs); // Ready only: starts a window pass.
    State state() const { return state_; }
    bool busy() const { return state_ == State::Indexing || state_ == State::Windowing; }
    unsigned progress() const; // Of the pass running, 0..100.
    const SessionInfo &info() const { return info_; }
    uint32_t records() const { return records_; }
    uint32_t invalid() const { return invalid_; }
    uint64_t firstUs() const { return first_; }
    uint64_t lastUs() const { return last_; }
    const TrendWindow &trend() const { return trend_; }
    const char *error() const { return error_; }

private:
    struct Checkpoint { uint32_t startMs = 0, offset = 0; };
    bool fail(const char *message);
    void line(const char *text, size_t length, uint32_t offset);
    void finishPass();
    FIL file_{};
    uint64_t lastStart_ = 0; // The pass's previous record: rows run forward in time.
    bool done_ = false;      // The window pass reached its end.
    bool open_ = false, overlong_ = false;
    State state_ = State::Idle;
    SessionInfo info_;
    uint32_t size_ = 0, position_ = 0, passStart_ = 0, lineStart_ = 0, lineLength_ = 0;
    uint32_t records_ = 0, invalid_ = 0, stride_ = 1;
    uint64_t first_ = 0, last_ = 0, windowEnd_ = 0;
    Checkpoint checkpoints_[Checkpoints];
    unsigned checkpointCount_ = 0;
    const char *error_ = nullptr;
    char lineBuffer_[SessionCsv::LineBytes];
    char chunk_[Chunk];
    TrendWindow trend_;
};
// The sessions in /openosc/LOGS (SESnnnnn.CSV, and SESnnnnn.TMP: unfinished, left by a power cut or a
// failed commit), newest number first, a page of PageSize at a time with what each file's header and
// trailer say. One directory pass a page: nothing but the page is kept.
class SessionCatalog {
public:
    static constexpr unsigned PageSize = 5, NameBytes = 13, PathBytes = 40;
    struct Entry {
        uint32_t number = 0, bytes = 0;
        bool temporary = false, described = false; // described: its header was read.
        char name[NameBytes]{};
        SessionInfo info;
    };
    explicit SessionCatalog(Filesystem &fs) : fs_(fs) {}
    // excluded: the number of the session being written (0: none), never listed.
    bool refresh(uint32_t excluded = 0);
    bool move(int direction); // One entry; at either end false (no wrap).
    bool eraseSelected();
    unsigned count() const { return count_; }
    unsigned selected() const { return selected_; }
    unsigned first() const { return first_; }
    unsigned visible() const { return visible_; }
    const Entry &entry(unsigned row) const { return entries_[row < PageSize ? row : 0]; }
    const Entry *current() const { return count_ && selected_ - first_ < visible_ ? &entries_[selected_ - first_] : nullptr; }
    bool path(char *output, size_t capacity) const;
    const char *error() const { return error_; }
    // SESnnnnn.CSV / .TMP (any case): its number and whether it is unfinished.
    static bool parseName(const char *name, uint32_t &number, bool &temporary);
    static uint32_t key(uint32_t number, bool temporary) { return number * 2 + (temporary ? 1 : 0); }

private:
    enum class Side : uint8_t { Below, Above };
    bool collect(Side side, uint32_t limit); // The page of keys below (or above) limit, and every count.
    void describe(Entry &entry);
    void describeLines(Entry &entry, const char *text, size_t length, bool skipFirst);
    bool fail(const char *message) { error_ = message; return false; }
    Filesystem &fs_;
    // Scratch of one call, kept here and not on the stack (a page refresh runs in the key handlers).
    DIR directory_{};
    FILINFO found_{};
    FIL file_{};
    char buffer_[512];
    Entry entries_[PageSize];
    unsigned count_ = 0, selected_ = 0, first_ = 0, visible_ = 0;
    uint32_t excluded_ = 0;
    const char *error_ = nullptr;
};
} // namespace openosc::dmm
