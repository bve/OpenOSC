#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/scope/trace_move.hpp"

namespace openosc {
// Crossing into or out of X-Y restarts the picture and both directions leave HOLD. Entering is one
// step with the new capture settings: if the capture cannot restart, nothing has changed.
bool ApplicationController::changePlotMode(plot::Options &options, plot::Mode before, uint32_t now) {
    if (!plot::XyMode::crossed(before, options.mode)) return true;
    if (options.mode != plot::Mode::XY) return data_.runScope();
    auto request = data_.scopeRequest();
    auto planned = options;
    plot::XyMode::enter(request, planned);
    if (!applyScope(request, now, ScopeApply::Resume)) return false;
    options = planned;
    return true;
}
void ApplicationController::plotKeys(uint32_t keys, uint32_t now, bool fine) {
    for (unsigned c=0; c<2; ++c) plotControls_.enabled[c]=channelShown(c);
    plotControls_.view = view_.channelView(); // Swap CH1/CH2: the rows name inputs as the plot does.
    ui::DisplayProperties list(plotControls_, fine);
    if (list.listing()) { // The section list: Open shows a section, Back goes up.
        using Outcome = ui::PropertyNavigator::Outcome;
        const auto outcome = navigator_.keys(keys, list);
        if (outcome == Outcome::Back) { backPanel(); return; }
        if (outcome == Outcome::Open || outcome == Outcome::Activate) {
            plotControls_.group = plot::Controls::Group(navigator_.row());
            plotControls_.listing = false;
            navigator_.reset();
        }
        view_.properties(ui::DisplayProperties(plotControls_, fine), navigator_);
        return;
    }
    const auto key = fieldKeys(keys, list, now);
    const unsigned row = list.row(navigator_.row());
    if (key == FieldKey::Committed) applyDisplay(plot::Controls::Count, now);
    else if (key == FieldKey::Action) applyDisplay(row, now);
    if (key == FieldKey::Committed || key == FieldKey::Action) view_.properties(ui::DisplayProperties(plotControls_, fine), navigator_);
}
// A committed field (row Count) or an action row.
// Refused, the drafts return to the view in force (the panel never shows an unapplied value).
bool ApplicationController::applyDisplay(unsigned row, uint32_t now) {
    if (row == plot::Controls::CenterTrigger) {
        const bool centered = centerTrigger(now);
        plotControls_.options = view_.waveform().options();
        return centered;
    }
    const auto refuse = [&](const char *why) {
        view_.notify(Tooltip::Unavailable, why, now);
        plotControls_.options = view_.waveform().options();
        return false;
    };
    auto &widget = view_.waveform();
    if (row == plot::Controls::Clear) {
        widget.clearHistory();
        widget.clearReference();
        view_.notify(Tooltip::Done, "History and reference cleared", now);
        return true;
    }
    if (row == plot::Controls::Reference) {
        const auto &options = widget.options();
        const unsigned channel = plotControls_.channel;
        constexpr auto VisibleTrace = "Reference needs a visible Y-T channel";
        if ((options.mode != plot::Mode::Time && options.mode != plot::Mode::SpectrumOverlay) ||
            !options.channels[channel].visible) return refuse(VisibleTrace);
        scope::FrameLease frame;
        if (!data_.acquireScopeWait(frame)) return refuse("Reference needs a completed frame");
        widget.setFrame(frame.view());
        if (!widget.captureReference(channel)) return refuse(VisibleTrace);
        view_.notify(Tooltip::Done, view_.channelView().slot(channel) ? "CH2 reference captured" : "CH1 reference captured", now);
        return true;
    }
    const auto &frontend=data_.scopeRequest().frontend;
    const bool heldXy = data_.scopeHeld() && view_.waveform().options().mode == plot::Mode::XY;
    const bool ch1 = heldXy ? channelShown(0) : frontend.enabled[0];
    const bool ch2 = heldXy ? channelShown(1) : frontend.enabled[1];
    if (plotControls_.options.mode==plot::Mode::XY &&
        (!ch1 || !ch2 || !plotControls_.options.channels[0].visible || !plotControls_.options.channels[1].visible)) {
        return refuse("XY needs CH1 + CH2 enabled and visible");
    }
    if (!plotControls_.enabled[plotControls_.options.spectrum.channel])
        plotControls_.options.spectrum.channel ^= 1;
    if (!changePlotMode(plotControls_.options,view_.waveform().options().mode,now)) return refuse("Finish data transfer first");
    widget.configure(plotControls_.options);
    selectedChannel_ = plotControls_.channel;
    plotControls_.options = widget.options(); // As the view took it (X-Y adds its own).
    queuePlotSave(now);
    dataRefresh_ = true;
    return true;
}
} // namespace openosc
