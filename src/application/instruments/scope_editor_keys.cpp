#include "openosc/application/controller.hpp"

namespace openosc {
void ApplicationController::scopeEditorKeys(uint32_t keys, uint32_t now) {
    ui::ScopeEditor &editor = panel_ == Panel::Channel ? static_cast<ui::ScopeEditor &>(channelEditor_) : acquisitionEditor_;
    if (fieldKeys(keys, editor, now) == FieldKey::Committed) commitScopeEditor(editor, now);
}
// A committed Channel/Acquisition field: the edited request (a channel that is not the last one
// on, X-Y's two channels, a Single that resumes: applyEdited) and the view's inversion. Nothing
// restarts when only the edited channel (a selection) changed. Refused, the editor shows the
// request in force again and the scale/position selection stays.
void ApplicationController::commitScopeEditor(ui::ScopeEditor &editor, uint32_t now) {
    const auto &before = view_.waveform().options();
    bool changedPlot = false; // The view's rows: Invert, Trace brightness (GitHub #86), Density and its colour (GitHub #85).
    for (unsigned c = 0; c < 2; ++c) {
        const auto &a = before.channels[c], &b = editor.plot().channels[c];
        changedPlot |= a.invert != b.invert || a.brightness != b.brightness || a.grade != b.grade || a.gradeColor != b.gradeColor;
    }
    const bool changedRequest = !editor.request().same(data_.scopeRequest());
    if (changedRequest && !applyEdited(editor.request(), now)) {
        view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
        editor.begin(data_.scopeRequest(), view_.waveform().options(), editor.channel());
        view_.properties(editor, navigator_);
        return;
    }
    if (changedPlot) { view_.waveform().configure(editor.plot()); queuePlotSave(now); }
    // The channel the panel edits is the scale/position channel too, as DISPLAY's Channel row: a
    // selection of the UI, no capture change. A channel switched off hands it to the one left on;
    // the FFT and cursor channels follow what is shown (data_display.cpp), the trigger source the editor.
    selectedChannel_ = data_.scopeRequest().frontend.enabled[editor.channel()] ? editor.channel() : editor.channel() ^ 1;
    meterSelected_ = false;
    editor.begin(data_.scopeRequest(), view_.waveform().options(), editor.channel()); // What was taken.
    dataRefresh_ = true;
    view_.properties(editor, navigator_);
}
} // namespace openosc
