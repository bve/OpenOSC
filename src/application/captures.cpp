#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include <cstdio>
#include <new>

// Settings > Files & USB > Waveforms and Clear waveforms (GitHub #186): the waveform exports of
// /openosc/CAPTURES (CaptureFiles). The list: Up/Down (or Left/Right) select, F3 deletes the selected
// file after a confirmation, F4 back to Settings. Clear waveforms asks first, then deletes one file a
// main-loop pass with its progress shown; F4 (Stop) ends it, the files already deleted stay deleted.
// A waveform save in progress refuses both, so its commit never meets a clear. While the catalog
// exists, MMEM:DEL refuses CAPTURES files and MMEM:DATA? refuses them during a clear; a file a USB
// transfer holds stops a delete (InstrumentBackend, storage::captureReaders).
namespace openosc {
bool ApplicationController::ensureCaptures(uint32_t now) {
    if (captures_) return true;
    void *memory = lv_malloc(sizeof(CaptureFiles));
    if (!memory) { view_.notify(Tooltip::Critical, "Not enough memory for the waveform list", now); return false; }
    captures_ = new (memory) CaptureFiles(fs_);
    return true;
}
void ApplicationController::closeCaptures() {
    if (captures_) {
        captures_->~CaptureFiles(); // Stops a clear in progress (its directory closed).
        lv_free(captures_);
        captures_ = nullptr;
    }
    confirmationCaptures_ = false;
}
void ApplicationController::openCaptures(uint32_t now) {
    if (!cancelAuto(now) || !ensureCaptures(now)) return;
    state_.settingsOpen = false; panel_ = Panel::Captures;
    if (!captures_->refresh()) view_.notify(Tooltip::Critical, captures_->error(), now);
    showCaptures();
    refreshSoftKeys();
}
// The page: each name and its size. An unread directory says why in place of "No waveforms".
void ApplicationController::showCaptures() {
    if (!captures_ || !uiReady_) return;
    static_assert(ui::ImageList::Rows == CaptureFiles::PageSize);
    ui::ImageList page;
    const auto &files = *captures_;
    page.visible = files.visible(); page.selected = files.selected(); page.first = files.first(); page.total = files.count();
    page.empty = files.listed() ? "No waveforms" : files.error() ? files.error() : "Storage not available";
    page.location = CaptureFiles::Directory;
    for (unsigned i = 0; i < page.visible; ++i) { page.names[i] = files.entry(i).name; page.values[i] = files.size(i); }
    view_.captures(page);
}
void ApplicationController::leaveCaptures(uint32_t now) {
    if (captures_ && captures_->clearing()) {
        char text[64];
        std::snprintf(text, sizeof text, "Waveform cleanup stopped: %u/%u deleted", captures_->erased(), captures_->count());
        view_.notify(Tooltip::Advisory, text, now);
    }
    closeCaptures();
    panel_ = Panel::None; state_.settingsOpen = true;
    navigator_.reset(settingsMenu_.selected());
    showSettings();
}
void ApplicationController::captureKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    if (keys & Keypad::Mode) { leaveCaptures(now); closeSettings(); return; }
    if (keys & Keypad::F4) { leaveCaptures(now); return; }
    if (!captures_ || captures_->clearing()) return;
    if (keys & Keypad::F3) {
        if (!captures_->count()) return;
        if (data_.scopeRecording()) { view_.notify(Tooltip::Unavailable, "Finish waveform save first", now); return; }
        confirmOperation(ui::Confirmation("DELETE THIS WAVEFORM?", "Delete selected waveform", captures_->name()),
                         &ApplicationController::deleteCapture);
        return;
    }
    const bool previous = keys & (Keypad::Up | Keypad::Left), next = keys & (Keypad::Down | Keypad::Right);
    if (previous || next) {
        if (!captures_->move(next ? 1 : -1) && captures_->error()) view_.notify(Tooltip::Critical, captures_->error(), now);
        showCaptures();
    }
}
void ApplicationController::deleteCapture(uint32_t now) {
    confirmationHandler_ = nullptr; confirmationCaptures_ = false;
    if (!captures_) { leaveCaptures(now); return; }
    panel_ = Panel::Captures;
    if (data_.scopeRecording()) view_.notify(Tooltip::Unavailable, "Finish waveform save first", now);
    else if (captures_->eraseSelected()) view_.notify(Tooltip::Done, "Waveform deleted", now);
    else view_.notify(captures_->error() ? Tooltip::Critical : Tooltip::Unavailable,
                      captures_->error() ? captures_->error() : "No waveform selected", now);
    showCaptures();
    refreshSoftKeys();
}
// Clear waveforms, confirmed from Settings: the list opens on the clear (serviceCaptures).
void ApplicationController::clearCaptures(uint32_t now) {
    confirmationHandler_ = nullptr;
    if (data_.scopeRecording()) { view_.notify(Tooltip::Unavailable, "Finish waveform save first", now); leaveConfirmation(now); return; }
    if (!ensureCaptures(now)) { leaveConfirmation(now); return; }
    panel_ = Panel::Captures; state_.settingsOpen = false;
    if (!captures_->beginClear()) view_.notify(Tooltip::Critical, captures_->error(), now);
    captureProgress_ = 0;
    showCaptures();
    refreshSoftKeys();
}
void ApplicationController::serviceCaptures(uint32_t now) {
    if (!captures_) return;
    if (panel_ != Panel::Captures && !(panel_ == Panel::Confirmation && confirmationCaptures_)) { closeCaptures(); return; }
    if (panel_ != Panel::Captures || !captures_->clearing()) return;
    const unsigned total = captures_->count();
    if (!captures_->clearStep()) {
        const char *error = captures_->error();
        captures_->refresh(); // What is left; an unreadable directory says so in the list.
        showCaptures(); refreshSoftKeys();
        view_.notify(Tooltip::Critical, error, now);
        return;
    }
    if (captures_->clearing() && captures_->erased() == captureProgress_) return;
    const bool complete = !captures_->clearing();
    if (complete) { showCaptures(); refreshSoftKeys(); }
    captureProgress_ = captures_->erased();
    char progress[48];
    std::snprintf(progress, sizeof progress, "%s: %u/%u", complete ? "Waveforms cleared" : "Deleting waveforms", captureProgress_, total);
    view_.notify(complete ? Tooltip::Done : Tooltip::Progress, progress, now);
}
// F3 with a file, not during a clear; F4 Back, or Stop during a clear.
ui::SoftKeys ApplicationController::captureSoftKeys() const {
    ui::SoftKeys keys;
    const bool clearing = captures_ && captures_->clearing();
    if (captures_ && captures_->count() && !clearing) keys.keys[2] = {"Delete", "", true};
    keys.keys[3] = {clearing ? "Stop" : "Back", "", true};
    return keys;
}
} // namespace openosc
