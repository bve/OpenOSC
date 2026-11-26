#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/confirmation.hpp"
#include <cstdio>
#include <cstring>
#include <new>

// DMM session logging in the controller (GitHub #144, docs/DMM_SESSION_LOGGING.md): Measurement >
// Session log (the settings, start/stop), the end of a session said once, and the saved sessions'
// list and trend. The session itself is DataAccess's (DmmSessionRecorder); this only asks for it.
namespace openosc {
namespace {
uint32_t sessionNumber(const char *path) { // The recorder's SESnnnnn.CSV: its number (0: none).
    const char *name = path ? std::strrchr(path, '/') : nullptr;
    uint32_t number = 0;
    bool temporary = false;
    return name && dmm::SessionCatalog::parseName(name + 1, number, temporary) ? number : 0;
}
} // namespace
ui::MeterLogProperties ApplicationController::meterLogProperties() const {
    const auto &session = data_.meterSession();
    ui::MeterLogProperties::State state;
    state.recording = session.recording();
    state.committing = session.active() && !session.recording();
    state.elapsedS = uint32_t((state.recording ? session.log().elapsedUs(data_.nowUs()) : session.log().summary().elapsedUs) / 1000000);
    return ui::MeterLogProperties(prefs_.meter.log, state);
}
void ApplicationController::openMeterLog(unsigned row) {
    logEditor_ = meterLogProperties();
    openPanel(Panel::MeterLog, logEditor_, row);
}
// Each committed field is saved at once; a running session keeps what it started with (the panel says).
void ApplicationController::meterLogKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = navigator_.keys(keys, logEditor_);
    if (outcome == Outcome::Back) {
        meterEditor_ = meterProperties();
        openPanel(Panel::Meter, meterEditor_, ui::MeterProperties::SessionLog);
        return;
    }
    if (outcome == Outcome::Open && navigator_.row() == ui::MeterLogProperties::Sessions) { openSessions(now); return; }
    if (outcome == Outcome::Activate) meterLogAction(now);
    if (outcome == Outcome::Changed) {
        const auto &next = logEditor_.settings();
        if (next.valid() && next != prefs_.meter.log) { prefs_.meter.log = next; changed(now); }
        if (!prefs_.meter.log.fits()) view_.notify(Tooltip::Usage, "Too many rows: longer interval or shorter duration", now);
    }
    if (outcome == Outcome::Changed || outcome == Outcome::Activate) logEditor_ = meterLogProperties();
    view_.properties(logEditor_, navigator_);
}
void ApplicationController::meterLogAction(uint32_t now) {
    auto &session = data_.meterSession();
    if (session.recording()) {
        data_.stopMeterSession();
        view_.notify(Tooltip::Progress, "Saving session...", now);
        return;
    }
    if (session.active()) { view_.notify(Tooltip::Unavailable, "The last session is still being saved", now); return; }
    if (!prefs_.meter.log.fits()) { view_.notify(Tooltip::Unavailable, "Too many rows: longer interval or shorter duration", now); return; }
    if (!data_.startMeterSession(prefs_.meter.log)) {
        const auto status = session.status();
        view_.notify(Tooltip::Critical, status == DataStatus::NoData ? "Session needs a DMM reading first"
                                       : status == DataStatus::NoSpace ? "Not enough free space for this session"
                                       : "Cannot start the session", now);
        return;
    }
    sessionWatched_ = true;
    const char *name = std::strrchr(session.path(), '/');
    char text[48];
    std::snprintf(text, sizeof text, "Recording %s", name ? name + 1 : session.path());
    view_.notify(Tooltip::Done, text, now);
}
// Once a pass: a session that ended (its stop, its duration, the size limit, leaving the DMM) is said
// with its file; the open panel's clock each second; the browser's slice of reading, and the browser
// itself goes once its list is no longer in front (MODE, Remote, a shutdown, any other path).
void ApplicationController::serviceMeterLog(uint32_t now) {
    auto &session = data_.meterSession();
    if (session.active()) sessionWatched_ = true;
    else if (sessionWatched_) {
        sessionWatched_ = false;
        if (uiReady_) {
            const char *name = std::strrchr(session.path(), '/');
            char text[64];
            if (session.status() != DataStatus::Ok)
                std::snprintf(text, sizeof text, "Session log failed: %s", session.status() == DataStatus::NoSpace ? "storage full" : "storage error");
            else if (session.end() == dmm::LogEnd::StorageFull) std::snprintf(text, sizeof text, "Session full, saved %s", name ? name + 1 : "");
            else std::snprintf(text, sizeof text, "Session saved: %s", name ? name + 1 : "");
            view_.notify(session.status() == DataStatus::Ok ? Tooltip::Done : Tooltip::Critical, text, now);
        }
        if (panel_ == Panel::MeterLog) { logEditor_ = meterLogProperties(); view_.properties(logEditor_, navigator_); }
        refreshMeterPanel();
        if (sessions_ && panel_ == Panel::Sessions && !sessions_->viewing()) { sessions_->refresh(0); showSessions(); }
    }
    if (panel_ == Panel::MeterLog && session.active() && navigator_.mode() == ui::PropertyNavigator::Mode::Browse &&
        uint32_t(now - logShownAt_) >= 1000) {
        logShownAt_ = now;
        logEditor_ = meterLogProperties();
        view_.properties(logEditor_, navigator_);
    }
    if (!sessions_) return;
    if (panel_ != Panel::Sessions && !(panel_ == Panel::Confirmation && confirmationSessions_)) { closeSessions(false); return; }
    if (panel_ == Panel::Sessions && sessions_->service() && sessions_->viewing() && uiReady_)
        view_.sessionTrend(sessions_->name(), sessions_->view(), sessions_->trend());
}
void ApplicationController::openSessions(uint32_t now) {
    if (!sessions_) {
        void *memory = lv_malloc(sizeof(SessionBrowser));
        if (!memory) { view_.notify(Tooltip::Critical, "Not enough memory for the session list", now); return; }
        sessions_ = new (memory) SessionBrowser(fs_);
    }
    panel_ = Panel::Sessions;
    sessions_->close();
    if (!sessions_->refresh(sessionNumber(data_.meterSession().active() ? data_.meterSession().path() : nullptr)) &&
        sessions_->catalog().error())
        view_.notify(Tooltip::Critical, sessions_->catalog().error(), now);
    showSessions();
}
void ApplicationController::closeSessions(bool toPanel) {
    if (sessions_) {
        if (uiReady_) view_.sessionsClosed(); // Its LVGL object goes before the view it draws from.
        sessions_->~SessionBrowser(); // Closes its file.
        lv_free(sessions_);
        sessions_ = nullptr;
    }
    confirmationSessions_ = false;
    if (toPanel) openMeterLog(ui::MeterLogProperties::Sessions);
}
void ApplicationController::showSessions() {
    if (!sessions_ || !uiReady_) return;
    if (sessions_->viewing()) view_.sessionTrend(sessions_->name(), sessions_->view(), sessions_->trend(), true);
    else view_.sessions(sessions_->list());
}
// The list: Up/Down select, MENU, Right or F1 open, F3 delete (confirmed), F4 back to Session log. The trend:
// Left/Right the cursor (a held key eight columns a step; past an edge the view pans), Up/F1 zoom in,
// Down/F2 out, F3 the whole session, F4 the list. MODE leaves for the next instrument (onKeys).
void ApplicationController::sessionKeys(const KeyEvents &events, uint32_t keys, uint32_t now) {
    if (!keys || !sessions_) return;
    auto &browser = *sessions_;
    if (!browser.viewing()) {
        if (keys & Keypad::F4) { closeSessions(true); return; }
        if ((keys & Keypad::F3) && browser.catalog().count()) {
            confirmOperation(ui::Confirmation("DELETE THIS SESSION?", "Delete selected session"), &ApplicationController::deleteSession);
            return;
        }
        if (keys & (Keypad::Up | Keypad::Down)) {
            if (!browser.catalog().move(keys & Keypad::Down ? 1 : -1) && browser.catalog().error())
                view_.notify(Tooltip::Critical, browser.catalog().error(), now);
            showSessions();
            return;
        }
        if ((keys & (Keypad::Menu | Keypad::Right | Keypad::F1)) && browser.catalog().current()) {
            if (!browser.open()) view_.notify(Tooltip::Critical, browser.error() ? browser.error() : "Cannot open session", now);
            showSessions();
        }
        return;
    }
    if (keys & Keypad::F4) { // The list again, as it is now (a session may have ended meanwhile).
        view_.sessionsClosed(); browser.close();
        browser.refresh(sessionNumber(data_.meterSession().active() ? data_.meterSession().path() : nullptr));
        showSessions();
        return;
    }
    bool changed = false;
    if (keys & (Keypad::Left | Keypad::Right)) {
        const int step = (events.repeat & (Keypad::Left | Keypad::Right)) ? 8 : 1;
        changed |= browser.cursor(keys & Keypad::Right ? step : -step);
    }
    if (keys & (Keypad::Up | Keypad::F1)) changed |= browser.zoom(1);
    if (keys & (Keypad::Down | Keypad::F2)) changed |= browser.zoom(-1);
    if (keys & Keypad::F3) changed |= browser.fit();
    if (changed) view_.sessionTrend(browser.name(), browser.view(), browser.trend());
}
void ApplicationController::deleteSession(uint32_t now) {
    confirmationHandler_ = nullptr;
    confirmationSessions_ = false;
    panel_ = Panel::Sessions;
    if (sessions_ && !sessions_->catalog().eraseSelected())
        view_.notify(Tooltip::Critical, sessions_->catalog().error() ? sessions_->catalog().error() : "Cannot delete session", now);
    showSessions();
}
ui::SoftKeys ApplicationController::sessionSoftKeys() const {
    ui::SoftKeys keys;
    keys.keys[3] = {"Back", "", true};
    if (!sessions_) return keys;
    if (!sessions_->viewing()) {
        if (sessions_->catalog().count()) {
            keys.keys[0] = {"Open", "MENU", true};
            keys.keys[2] = {"Delete", "", true};
        }
        return keys;
    }
    keys.keys[0] = {"Zoom", "In", true};
    keys.keys[1] = {"Zoom", "Out", true};
    keys.keys[2] = {"Fit", "All", true};
    keys.keys[3] = {"Back", "List", true};
    return keys;
}
} // namespace openosc
