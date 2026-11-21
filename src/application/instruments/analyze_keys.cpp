#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/scope_actions.hpp"

namespace openosc {
void ApplicationController::analyzeKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Actions = ui::ScopeActions;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto &shown = view_.waveform().options();
    Actions actions(shown);
    const auto outcome = navigator_.keys(keys, actions);
    if (outcome == Outcome::Back) { closePanel(); return; }
    if (outcome == Outcome::Changed) { // The cursor lines, taken at once.
        auto cursors = shown.cursors;
        cursors.time = actions.cursors().time; cursors.voltage = actions.cursors().voltage;
        view_.waveform().cursorLines(cursors); queuePlotSave(now);
        syncCursorFocus();
        if (cursors.time || cursors.voltage) {
            if (shown.mode == plot::Mode::XY) view_.notify("Cursors need a Y-T or FFT view", now);
            else enterCursors(now); // The menu stays; the arrows move the cursors once it closes.
        }
        view_.properties(Actions(view_.waveform().options()), navigator_);
        return;
    }
    if (outcome != Outcome::Activate && outcome != Outcome::Open) { view_.properties(actions, navigator_); return; }
    for (unsigned c=0; c<2; ++c) plotControls_.enabled[c]=channelShown(c);
    const unsigned row = navigator_.row();
    analyzeRow_ = row; // A sub-menu's Back returns here.
    switch (row) {
    case Actions::Spectrum:
        plotControls_.options=view_.waveform().options();
        plotControls_.channel=plotControls_.enabled[selectedChannel_] ? selectedChannel_ : selectedChannel_^1;
        plotControls_.open(plot::Controls::FftSource);
        // FFT and Y-T+FFT already show the spectrum: its fields are edited in that view, which and its
        // FFT source stay as they are, and nothing is applied (GitHub #17); in FFT the panel's channel
        // is its source, in Y-T+FFT the selected one. Y-T and X-Y become FFT at once, on the selected
        // channel; its fields follow.
        if (plotControls_.options.mode==plot::Mode::Spectrum) plotControls_.channel=plotControls_.options.spectrum.channel;
        else if (plotControls_.options.mode!=plot::Mode::SpectrumOverlay) {
            plotControls_.options.mode=plot::Mode::Spectrum;
            plotControls_.options.spectrum.channel=plotControls_.channel;
            plotControls_.options.cursors.channel=plotControls_.channel;
            if (!applyDisplay(plot::Controls::Count, now)) return;
        }
        sectionDirect_ = true; // Back returns to Analyze.
        openPanel(Panel::Display, ui::DisplayProperties(plotControls_), plotControls_.index(plot::Controls::FftSource)); return;
    case Actions::Measurements:
        measurementEditor_.begin(prefs_.scope.measurements);
        openPanel(Panel::Measurements, measurementEditor_); return;
    case Actions::Display:
        plotControls_.options = view_.waveform().options(); plotControls_.channel = selectedChannel_;
        plotControls_.options.cursors.channel = selectedChannel_;
        plotControls_.listing = true; sectionDirect_ = false;
        openPanel(Panel::Display, ui::DisplayProperties(plotControls_), unsigned(plotControls_.group)); return;
    case Actions::Layout:
        scopeLayout_ = ui::ArrangementProperties<ScopeBlock>("SCOPE LAYOUT", prefs_.scope.arrangement);
        openPanel(Panel::ScopeLayout, scopeLayout_); return;
    case Actions::Colors:
        colorsEditor_ = ui::ScopeAppearanceProperties(themes_.theme(), prefs_.scope.appearance, colorsEditor_.element());
        openPanel(Panel::ScopeColors, colorsEditor_); return;
    case Actions::Acquisition:
        acquisitionEditor_.begin(data_.scopeRequest(), view_.waveform().options(), selectedChannel_);
        acquisitionEditor_.snapshot(view_.waveform().model().metadata());
        openPanel(Panel::Acquisition, acquisitionEditor_); return;
    case Actions::Decoder: openDecoder(0); return; // Protocol: Off until chosen.
#if OPENOSC_BOARD == 703
    case Actions::Bode: openBode(now); return; // Its own panel, or a notice why not.
#endif
    case Actions::CenterChannels:
        if (!centerChannels(now)) return;
        break;
    case Actions::ResetPan: resetPan(now); break;
    case Actions::TriggerMidpoint:
        if (!triggerMidpoint(now)) return;
        break;
    case Actions::ArmSingle: {
        auto request = data_.scopeRequest(); request.trigger.mode = scope::TriggerMode::Single;
        if (!applyScope(request, now, ScopeApply::Resume)) { view_.notify("Cannot arm single capture", now); return; }
        break;
    }
    case Actions::SaveRaw:
    case Actions::SaveCsv:
        if (!data_.recordScope(row == Actions::SaveRaw ? ScopeRecorder::Format::Raw : ScopeRecorder::Format::Csv)) {
            view_.notify("Cannot start waveform save", now); return;
        }
        view_.notify("Saving waveform...", now);
        recordWatched_ = true;
        break;
    default: return;
    }
    closePanel();
}
// Both channels' zero lines to the middle of the plot: Analyze -> Center channels and the F2 hold,
// as on the factory firmware. RUN: both analog zeros to the middle (80) and both digital positions
// to 500. HOLD: the record on show is not re-captured and its metadata never change, so only the
// view moves: each digital position becomes the one that centres the shown record's own analog
// zero (Axis::base); the request, and with it the next capture's analog zero, stays as it is.
// After RUN the same analog zero comes back, so the zero stays centred (also when HOLD exploration
// returns the rest of the view); a different analog zero staged for RUN moves it, as any staged
// change would.
bool ApplicationController::centerChannels(uint32_t now) {
    auto options=view_.waveform().options();
    if (data_.scopeHeld()) {
        const auto &model=view_.waveform().model();
        for (unsigned c=0; c<2; ++c) {
            options.channels[c].positionPermille=int16_t(1000-(model.hasFrame() ? plot::Axis::base(model.metadata().channels[c]) : 500));
            // A deliberate centring, not exploration of the record: RUN keeps it (zoom/gain/pan still return).
            if (heldViewActive_) heldView_.channels[c].positionPermille=options.channels[c].positionPermille;
        }
    } else {
        auto request=data_.scopeRequest();
        request.frontend.position[0]=request.frontend.position[1]=80;
        if (!applyEdited(request,now)) { view_.notify("Finish data transfer first",now); return false; }
        options.channels[0].positionPermille=options.channels[1].positionPermille=500;
    }
    view_.waveform().configure(options); queuePlotSave(now);
    return true;
}
void ApplicationController::measurementKeys(uint32_t keys, uint32_t now) {
    if (fieldKeys(keys, measurementEditor_, now) != FieldKey::Committed) return;
    prefs_.scope.measurements = state_.scope.measurements = measurementEditor_.selection();
    changed(now); dataRefresh_ = true;
    view_.properties(measurementEditor_, navigator_);
}
} // namespace openosc
