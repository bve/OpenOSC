#include "openosc/application/controller.hpp"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/meter_function.hpp"

namespace openosc {
void ApplicationController::scopeEditorKeys(uint32_t keys, uint32_t now) {
    ui::ScopeEditor &editor = panel_ == Panel::Channel ? static_cast<ui::ScopeEditor &>(channelEditor_) : acquisitionEditor_;
    if (fieldKeys(keys, editor, now) == FieldKey::Committed) commitScopeEditor(editor, now);
}
// A committed Channel/Acquisition field: the edited request (a channel that is not the last one
// on, X-Y's two channels, a Single that resumes: applyEdited), the view's rows (inversion, a channel
// shown or hidden, the DMM trace on) and the DMM's function. Nothing restarts when only the edited
// channel (a selection) changed. Refused, the editor shows the settings in force again and the
// scale/position selection stays.
void ApplicationController::commitScopeEditor(ui::ScopeEditor &editor, uint32_t now) {
    const auto &before = view_.waveform().options();
    // The view's rows: Invert, Trace brightness (GitHub #86), Density and its colour (GitHub #85), the DMM
    // trace on and the channel kept only as its time axis.
    bool changedPlot = before.meter.shown != editor.plot().meter.shown || before.meter.axis != editor.plot().meter.axis;
    for (unsigned c = 0; c < 2; ++c) {
        const auto &a = before.channels[c], &b = editor.plot().channels[c];
        changedPlot |= a.invert != b.invert || a.brightness != b.brightness || a.grade != b.grade || a.gradeColor != b.gradeColor;
    }
    const bool channel = panel_ == Panel::Channel;
    const auto again = [&] { // The editor as the settings in force are.
        if (channel) channelEditor_.begin(data_.scopeRequest(), view_.waveform().options(), editor.channel(), prefs_.meterFunction);
        else editor.begin(data_.scopeRequest(), view_.waveform().options(), editor.channel());
        view_.properties(editor, navigator_);
    };
    const bool changedRequest = !editor.request().same(data_.scopeRequest());
    if (changedRequest && !applyEdited(editor.request(), now)) {
        view_.notify(Tooltip::Unavailable, "Finish data transfer first", now);
        again();
        return;
    }
    if (changedPlot) { view_.waveform().configure(editor.plot()); queuePlotSave(now); }
    // The DMM's function from its own page only.
    if (const char *refused = channel && channelEditor_.meter() ? selectMeterFunction(channelEditor_.meterFunction(), now) : nullptr) {
        view_.notify(Tooltip::Unavailable, refused, now);
        again();
        return;
    }
    // The channel the Channel panel edits is the scale/position channel too, as DISPLAY's Channel row:
    // a selection of the UI, no capture change. A channel switched off hands it to the one left shown,
    // none to the DMM trace (followShownChannel); the FFT and cursor channels follow what is shown
    // (data_display.cpp), the trigger source the editor.
    if (channel && editor.channel() < 2) {
        selectedChannel_ = channelShown(editor.channel()) ? editor.channel() : editor.channel() ^ 1;
        meterSelected_ = false;
    } else if (channel) meterSelected_ = view_.waveform().options().meter.shown;
    followShownChannel();
    again(); // What was taken.
    dataRefresh_ = true;
}
const char *ApplicationController::selectMeterFunction(unsigned index, uint32_t now) {
    if (index == prefs_.meterFunction || index >= MeterFunctionCount) return nullptr;
    const auto function = dmm::Function(index);
    if (function == dmm::Function::Temperature && !prefs_.meter.temperature.configured())
        return "Type K: set Manual junction in the DMM's MENU";
    if (data_.meterTraceRunning() && !data_.selectDmmFunction(function)) return "Measurement command busy";
    prefs_.meterFunction = state_.meter.function = index;
    changed(now);
    return nullptr;
}
} // namespace openosc
