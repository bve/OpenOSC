#include "openosc/application/controller.hpp"
#include <cstdio>
#include "openosc/platform/input.hpp"
#include "openosc/ui/scope/trace_move.hpp"
#include "openosc/generator/frequency.hpp"

namespace openosc {
void ApplicationController::openPanel(Panel panel, const ui::PropertyList &list, unsigned row) {
    panel_ = panel;
    navigator_.reset(row);
    view_.properties(list, navigator_);
}
void ApplicationController::backPanel() {
    if (panel_ == Panel::Display && !plotControls_.listing && !sectionDirect_) { // A section: back to the list, on its row.
        plotControls_.listing = true;
        openPanel(Panel::Display, ui::DisplayProperties(plotControls_), unsigned(plotControls_.group));
        return;
    }
    const bool child = panel_ == Panel::Display || panel_ == Panel::Measurements || panel_ == Panel::Acquisition ||
                       panel_ == Panel::Decoder;
    if (child && analyzeRow_ != ~0U) { openAnalyze(analyzeRow_); return; }
    // The F4 hold's cursor settings (openShortcut: the section alone, no Analyze under it).
    const bool cursorShortcut = panel_ == Panel::Display && sectionDirect_ && plotControls_.group == plot::Controls::Group::Cursors;
    closePanel();
    if (cursorShortcut) focusCursors();
}
// Back on the waveform from the cursor settings (GitHub #125): the Cursor card selected on the first line
// drawn (Time A, or Level A with Voltage only), so the arrows move it at once; the lines and channel are
// those the settings left. No line to move (both off, X-Y): the focus stays where it was, or leaves a
// Cursor card that has nothing to move (syncCursorFocus).
void ApplicationController::focusCursors() {
    const auto &options = view_.waveform().options();
    if (!plot::CursorControl::available(options)) { syncCursorFocus(); return; }
    cursors_.enter(options.cursors);
    setScopeFocus(plot::Focus::Cursor);
}
void ApplicationController::closePanel() {
    analyzeRow_ = ~0U;
    panel_ = Panel::None;
    dataRefresh_ = true;
    view_.measurement(state_);
}
// One field at a time (PropertyNavigator): a Choice taken, a Toggle switched or a Number finished
// with MENU is Committed for the caller to apply, the panel staying open; a refused choice says why.
ApplicationController::FieldKey ApplicationController::fieldKeys(uint32_t keys, ui::PropertyList &list, uint32_t now) {
    if (!keys) return FieldKey::None;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = navigator_.keys(keys, list);
    if (outcome == Outcome::Refused && list.notice()) view_.notify(Tooltip::Unavailable, list.notice(), now);
    if (outcome == Outcome::Back) { backPanel(); return FieldKey::Closed; }
    if (outcome == Outcome::Changed) return FieldKey::Committed;
    if (outcome == Outcome::Activate) return FieldKey::Action;
    view_.properties(list, navigator_);
    return FieldKey::None;
}
// The trigger panel's committed field. Refused (a host holds the record), it shows the trigger in force.
// Its level (a step, 50 %) is set on purpose: the anchor of the following, also as the same code.
void ApplicationController::commitTrigger(uint32_t now, bool level) {
    auto request = data_.scopeRequest();
    request.trigger = triggerEditor_.settings();
    if (request.same(data_.scopeRequest())) { if (level) triggerFollow_.forget(); } // Nothing to apply: set as it is.
    else if (!applyEdited(request, now, level ? LevelIntent::Set : LevelIntent::Follow)) view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
    triggerEditor_.begin(data_.scopeRequest().trigger);
    view_.properties(triggerEditor_, navigator_);
}
void ApplicationController::triggerKeys(uint32_t keys, uint32_t now, bool fine) {
    auto request = data_.scopeRequest();
    request.trigger = triggerEditor_.settings();
    const auto bounds = triggerBounds(request);
    const bool editing = navigator_.row() == ui::TriggerEditor::Level && navigator_.mode() == ui::PropertyNavigator::Mode::Number;
    const int vertical = (keys & Keypad::Up ? 1 : 0) - (keys & Keypad::Down ? 1 : 0);
    triggerEditor_.levelEdit(bounds, editing && vertical ? std::abs(triggerStep(vertical, fine)) : 1);
    const auto key = fieldKeys(keys, triggerEditor_, now);
    if (key == FieldKey::Action) {
        if (navigator_.row() == ui::TriggerEditor::Center) centerTrigger(now);
        else triggerMidpoint(now);
        triggerEditor_.begin(data_.scopeRequest().trigger);
        view_.properties(triggerEditor_, navigator_);
    } else if (key == FieldKey::Committed) commitTrigger(now, navigator_.row() == ui::TriggerEditor::Level);
    // The draft, a new source or a committed level at either end: the Level row's mark follows.
    if (panel_ == Panel::Trigger && markTriggerEditor()) view_.properties(triggerEditor_, navigator_);
}
// Each short F3 (plan S01): the trigger focus and the next mode, Auto, Normal, Single, Auto; the source
// is in the Trigger menu. In RUN, Single arms one record (as Analyze's Arm Single). Held (HOLD or a
// finished Single), the change is staged for the next capture and the held record stays: only an
// explicit RUN (or Rearm Single) starts it. The level is not touched (LevelIntent::Follow, #26).
void ApplicationController::triggerModeStep(uint32_t now) {
    auto request = data_.scopeRequest();
    request.trigger.mode = scope::TriggerMode((unsigned(request.trigger.mode) + 1) % 3);
    plot::XyMode::constrain(view_.waveform().options(), request);
    const ScopeApply mode = data_.scopeHeld() ? ScopeApply::StageIfHeld
                          : request.trigger.mode == scope::TriggerMode::Single ? ScopeApply::Resume : ScopeApply::Configure;
    if (!applyFollowing(request, now, mode)) { view_.notify(Tooltip::Unavailable, "Finish data transfer first", now); return; }
    dataRefresh_ = true;
}
void ApplicationController::frequencyKeys(uint32_t keys, uint32_t now) {
    frequencyPanelKeys(keys, now);
    refreshSoftKeys(); // Unit on F2, Cancel on F4; closed: the screen's own cards.
}
void ApplicationController::frequencyPanelKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    if (keys & Keypad::F4) { closePanel(); return; }
    if (switchGeneratorPanel(keys, now)) return; // F1 WAVEFORM, F3 PARAMETERS; F2 stays the unit key.
    if (keys & Keypad::Left) frequencyEditor_.move(-1);
    if (keys & Keypad::Right) frequencyEditor_.move(1);
    if (keys & Keypad::Up) frequencyEditor_.adjust(1);
    if (keys & Keypad::Down) frequencyEditor_.adjust(-1);
    if (keys & Keypad::F2) frequencyEditor_.nextUnit();
    if (keys & Keypad::Menu) {
        auto settings = data_.generator().settings();
        settings.frequencyHz = generator::Frequency::rational(frequencyEditor_.value());
        if (!applyGenerator(settings, now)) {
            view_.notify(Tooltip::Unavailable, "Unsupported output frequency", now);
            return;
        }
        closePanel();
        return;
    }
    view_.frequencyEditor(frequencyEditor_);
}
} // namespace openosc
