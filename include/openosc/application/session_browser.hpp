#pragma once
#include "openosc/data/session_reader.hpp"
#include "openosc/ui/components/session_trend.hpp"
#include "openosc/ui/images/browser.hpp"

namespace openosc {
// Measurement > Session log > Saved sessions (GitHub #144): the list of sessions and one session's
// trend, over dmm::SessionCatalog, dmm::SessionScan and dmm::TrendNavigator. No LVGL and no file is
// held while the list shows; a session's file is read in slices by service() (a bounded number of bytes
// a main-loop pass), first its index, then the window the view asks for: a zoom or pan waits for the
// pass in progress to be redone, never for the whole file. The controller owns one only while the list
// is open (allocated then, from the LVGL heap: no DTCM, no acquisition memory).
class SessionBrowser {
public:
    static constexpr uint32_t SliceBytes = 8192; // A pass's share: about 2 ms of NOR reading and parsing.
    explicit SessionBrowser(Filesystem &fs) : catalog_(fs) {}
    bool refresh(uint32_t excluded) { return catalog_.refresh(excluded); }
    const dmm::SessionCatalog &catalog() const { return catalog_; }
    dmm::SessionCatalog &catalog() { return catalog_; }
    ui::ImageList list() const; // The page as the file list draws it (names and what each one is).
    bool open();                // The selected session: its index pass starts. False: error().
    void close() { scan_.close(); viewing_ = false; }
    bool viewing() const { return viewing_; }
    // The view's keys: false when nothing changed.
    bool zoom(int direction) { return change(navigator_.zoom(direction)); }
    bool pan(int direction) { return change(navigator_.pan(direction)); }
    bool fit();
    bool cursor(int columns);
    // One slice of reading. True when the view should be drawn again (a pass ended, its progress moved).
    bool service(uint32_t budget = SliceBytes);
    bool busy() const { return scan_.busy() || pending_; }
    ui::SessionTrendData trend() const;
    const char *error() const { return scan_.state() == dmm::SessionScan::State::Failed ? scan_.error() : nullptr; }
    const char *name() const { return name_; }
    const dmm::SessionScan &scan() const { return scan_; }
    const dmm::TrendNavigator &navigator() const { return navigator_; }
    ui::SessionTrendView &view() { return view_; } // The trend's LVGL drawing, kept here (not in main()'s frame).

private:
    bool change(bool changed) { if (changed) pending_ = true; return changed; }
    dmm::SessionCatalog catalog_;
    dmm::SessionScan scan_;
    dmm::TrendNavigator navigator_;
    ui::SessionTrendView view_;
    bool viewing_ = false, pending_ = false, windowed_ = false;
    unsigned shownProgress_ = ~0U;
    char name_[dmm::SessionCatalog::NameBytes]{};
    mutable char values_[dmm::SessionCatalog::PageSize][32]{}; // The page's and the status' texts (list(), trend()).
    mutable char status_[32]{};
};
} // namespace openosc
