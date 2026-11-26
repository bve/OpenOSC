#include "openosc/data/trend_window.hpp"

// The trend's reduction and navigation (GitHub #144): no file, no LVGL (SessionScan feeds the window,
// ui::SessionTrendView draws it).
namespace openosc::dmm {
void TrendWindow::begin(uint64_t startUs, uint64_t endUs) {
    start_ = startUs;
    end_ = endUs > startUs ? endUs : startUs + 1;
    for (auto &column : columns_) column = TrendColumn{};
}
unsigned TrendWindow::columnOf(uint64_t us) const {
    if (us <= start_) return 0;
    if (us >= end_) return Columns - 1;
    return unsigned((us - start_) * Columns / (end_ - start_));
}
uint64_t TrendWindow::columnStart(unsigned index) const {
    return start_ + (end_ - start_) * (index < Columns ? index : Columns) / Columns;
}
void TrendWindow::add(const LogRecord &r) {
    const uint64_t s = r.startUs, e = r.endUs > s ? r.endUs : s + 1; // A record of no length still marks its column.
    if (e <= start_ || s >= end_) return;
    const unsigned c0 = columnOf(s > start_ ? s : start_), c1 = columnOf((e < end_ ? e : end_) - 1);
    for (unsigned c = c0; c <= c1; ++c) {
        auto &column = columns_[c];
        if (r.status != LogStatus::Ok) {
            column.flags |= r.status == LogStatus::Overload ? TrendColumn::Overload : TrendColumn::Gap;
            continue;
        }
        const float low = float(r.minimum), high = float(r.maximum);
        if (!column.value()) { column.minimum = low; column.maximum = high; }
        if (low < column.minimum) column.minimum = low;
        if (high > column.maximum) column.maximum = high;
        column.sum += r.average * r.readings;
        column.readings += r.readings;
        column.flags |= TrendColumn::Value | (r.overloads ? TrendColumn::Overload : 0);
    }
}
bool TrendWindow::range(double &low, double &high) const {
    bool any = false;
    for (const auto &column : columns_) {
        if (!column.value()) continue;
        if (!any || column.minimum < low) low = column.minimum;
        if (!any || column.maximum > high) high = column.maximum;
        any = true;
    }
    return any;
}

void TrendNavigator::fit(uint64_t firstUs, uint64_t lastUs) {
    first_ = start_ = firstUs;
    last_ = end_ = lastUs > firstUs ? lastUs : firstUs + 1;
    cursor_ = TrendWindow::Columns / 2;
}
bool TrendNavigator::place(uint64_t start, uint64_t span) {
    const uint64_t extent = last_ - first_;
    const uint64_t least = MinimumSpanUs < extent ? MinimumSpanUs : extent;
    if (span < least) span = least;
    if (span > extent) span = extent;
    if (start < first_) start = first_;
    if (start > last_ - span) start = last_ - span;
    const bool changed = start != start_ || start + span != end_;
    start_ = start;
    end_ = start + span;
    return changed;
}
bool TrendNavigator::zoom(int direction) {
    if (!direction) return false;
    const uint64_t span = end_ - start_;
    // The cursor's time stays under the cursor: the window grows or shrinks about it.
    const uint64_t at = start_ + span * (2 * cursor_ + 1) / (2 * TrendWindow::Columns);
    const uint64_t next = direction > 0 ? span / 2 : span * 2;
    const uint64_t before = next * (2 * cursor_ + 1) / (2 * TrendWindow::Columns);
    if (!place(at > first_ + before ? at - before : first_, next)) return false;
    const uint64_t now = end_ - start_;
    cursor_ = unsigned(at <= start_ ? 0 : at >= end_ ? TrendWindow::Columns - 1 : (at - start_) * TrendWindow::Columns / now);
    return true;
}
bool TrendNavigator::pan(int direction) {
    if (!direction) return false;
    const uint64_t span = end_ - start_, step = span / 2 ? span / 2 : 1;
    const uint64_t start = direction > 0 ? start_ + step : (start_ > first_ + step ? start_ - step : first_);
    return place(start, span);
}
bool TrendNavigator::cursor(int columns, bool &panned) {
    panned = false;
    const int last = int(TrendWindow::Columns) - 1;
    const int next = int(cursor_) + columns;
    if (next >= 0 && next <= last) { cursor_ = unsigned(next); return true; }
    const uint64_t span = end_ - start_, step = span / 4 ? span / 4 : 1;
    const uint64_t start = next > last ? start_ + step : (start_ > first_ + step ? start_ - step : first_);
    panned = place(start, span);
    const unsigned edge = next > last ? unsigned(last) : 0;
    const bool moved = panned || edge != cursor_;
    cursor_ = edge;
    return moved;
}
unsigned TrendNavigator::zoomFactor() const {
    const uint64_t span = end_ - start_, extent = last_ - first_;
    return span ? unsigned((extent + span / 2) / span) : 1;
}

} // namespace openosc::dmm
