#include "openosc/application/session_browser.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/ui/editors/meter_log_properties.hpp"
#include <cstdio>
#include <cstring>

namespace openosc {
ui::ImageList SessionBrowser::list() const {
    ui::ImageList page;
    static_assert(ui::ImageList::Rows == dmm::SessionCatalog::PageSize);
    page.visible = catalog_.visible();
    page.selected = catalog_.selected();
    page.first = catalog_.first();
    page.total = catalog_.count();
    page.empty = "No DMM sessions";
    page.location = storage::Layout::Logs;
    for (unsigned i = 0; i < page.visible; ++i) {
        const auto &entry = catalog_.entry(i);
        page.names[i] = entry.name;
        char *value = values_[i];
        if (!entry.described) std::snprintf(value, sizeof values_[i], entry.bytes ? "Not a session" : "Empty");
        else {
            char span[16];
            const auto &info = entry.info;
            ui::MeterLogProperties::clock(info.ended ? info.summary.elapsedUs : 0, span, sizeof span);
            const char *mode = info.settings.mode() == dmm::LogMode::Event ? "Event" : "Interval";
            if (!info.ended) std::snprintf(value, sizeof values_[i], "%s unfinished", mode);
            else std::snprintf(value, sizeof values_[i], "%s %s", mode, span);
        }
        page.values[i] = value;
    }
    return page;
}
bool SessionBrowser::open() {
    const auto *entry = catalog_.current();
    char path[dmm::SessionCatalog::PathBytes];
    if (!entry || !catalog_.path(path, sizeof path)) return false;
    std::snprintf(name_, sizeof name_, "%s", entry->name);
    viewing_ = true;
    pending_ = windowed_ = false;
    shownProgress_ = ~0U;
    return scan_.open(path);
}
bool SessionBrowser::fit() {
    if (!windowed_ && !scan_.records()) return false;
    const bool fitted = navigator_.fitted();
    navigator_.fit(scan_.firstUs(), scan_.lastUs());
    return change(!fitted);
}
bool SessionBrowser::cursor(int columns) {
    bool panned = false;
    const bool moved = navigator_.cursor(columns, panned);
    if (panned) pending_ = true;
    return moved;
}
// The index pass first; once it ends the whole session is fitted. A view change while a window pass
// runs starts that pass again for the new window.
bool SessionBrowser::service(uint32_t budget) {
    if (!viewing_) return false;
    using State = dmm::SessionScan::State;
    const State before = scan_.state();
    if (before == State::Ready && !windowed_ && scan_.records()) {
        navigator_.fit(scan_.firstUs(), scan_.lastUs());
        pending_ = true;
    }
    if (pending_ && (scan_.state() == State::Ready || scan_.state() == State::Windowing)) {
        pending_ = false;
        windowed_ = scan_.window(navigator_.startUs(), navigator_.endUs());
        shownProgress_ = ~0U;
        return true;
    }
    if (!scan_.busy()) return false;
    scan_.step(budget);
    if (!scan_.busy()) return true; // A pass ended (or failed): the view shows it.
    const unsigned progress = scan_.progress() / 10;
    if (progress == shownProgress_) return false;
    shownProgress_ = progress;
    return true;
}
ui::SessionTrendData SessionBrowser::trend() const {
    ui::SessionTrendData data;
    data.info = &scan_.info();
    data.records = scan_.records();
    data.invalid = scan_.invalid();
    data.firstUs = scan_.firstUs();
    data.lastUs = scan_.lastUs();
    data.cursor = navigator_.cursorColumn();
    data.zoom = navigator_.zoomFactor();
    data.trend = windowed_ ? &scan_.trend() : nullptr;
    char *status = status_;
    using State = dmm::SessionScan::State;
    if (scan_.state() == State::Failed) data.status = scan_.error();
    else if (scan_.state() == State::Indexing) { std::snprintf(status, sizeof status_, "Reading %u%%", scan_.progress()); data.status = status; }
    else if (scan_.state() == State::Windowing || pending_) data.status = "Drawing...";
    else if (!scan_.records()) data.status = "No records";
    return data;
}
} // namespace openosc
