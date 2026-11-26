#pragma once
#include "openosc/data/session_log.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::dmm {
// One trend column: the min/max envelope of every value record that overlaps its time (so a single
// spike or drop in a column stays visible however far the view is zoomed out), the readings' sum for
// their average, and what else happened in it (OL, a stretch without values).
struct TrendColumn {
    enum Flag : uint8_t { Value = 1, Overload = 2, Gap = 4 };
    float minimum = 0, maximum = 0;
    double sum = 0;       // Average x readings of each value record in it.
    uint32_t readings = 0;
    uint8_t flags = 0;
    bool value() const { return flags & Value; }
    double average() const { return readings ? sum / readings : 0; }
};
// The records of a window [startUs, endUs) reduced to Columns columns, no record kept: a record covers
// every column its [start, end) overlaps. Times are the session's (microseconds since its start).
class TrendWindow {
public:
    static constexpr unsigned Columns = 256; // The plot's width in pixels.
    void begin(uint64_t startUs, uint64_t endUs);
    void add(const LogRecord &record);
    uint64_t startUs() const { return start_; }
    uint64_t endUs() const { return end_; }
    const TrendColumn &column(unsigned index) const { return columns_[index < Columns ? index : Columns - 1]; }
    unsigned columnOf(uint64_t us) const;      // The column holding `us` (clamped to the window).
    uint64_t columnStart(unsigned index) const; // Its first microsecond.
    bool range(double &low, double &high) const; // The values' envelope over the window; false without values.

private:
    TrendColumn columns_[Columns];
    uint64_t start_ = 0, end_ = 1;
};
// The view over a session: a window of its extent, zoomed by halves around the cursor and panned by half
// windows; the cursor a column, which pans the view a quarter when pushed past an edge. No file access.
class TrendNavigator {
public:
    static constexpr uint64_t MinimumSpanUs = 5000000; // 5 s across the plot: a 1 s interval's row is 51 px wide.
    void fit(uint64_t firstUs, uint64_t lastUs);
    bool zoom(int direction);            // > 0 in (half the span), < 0 out (twice); false at the limit.
    bool pan(int direction);             // Half a window; false at the extent's edge.
    bool cursor(int columns, bool &panned); // False when it cannot move; panned: the window moved.
    uint64_t startUs() const { return start_; }
    uint64_t endUs() const { return end_; }
    unsigned cursorColumn() const { return cursor_; }
    unsigned zoomFactor() const; // The extent over the span, rounded (1 when fitted).
    bool fitted() const { return start_ == first_ && end_ == last_; }

private:
    bool place(uint64_t start, uint64_t span); // Clamped into the extent; true when it changed.
    uint64_t first_ = 0, last_ = 1, start_ = 0, end_ = 1;
    unsigned cursor_ = TrendWindow::Columns / 2;
};
} // namespace openosc::dmm
