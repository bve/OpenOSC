#include "openosc/application/controller.hpp"
#include "openosc/ui/editors/scope_actions.hpp"

namespace openosc {
// The saved looks to the view: block orders and Scope colours. Each is a no-op when the view
// already has it, and none reaches the capture, HOLD or the measurements.
void ApplicationController::applyLooks() {
    view_.scopeArrangement(prefs_.scope.arrangement);
    view_.generatorArrangement(prefs_.generatorArrangement);
    view_.meterArrangement(prefs_.meter.digitalArrangement, prefs_.meter.analogArrangement);
    view_.scopeAppearance(prefs_.scope.appearance);
}
void ApplicationController::buildView() { view_.build(themes_.theme()); applyLooks(); counterStale_ = true; }
// One LAYOUT list's keys: a committed position is saved and restyles the screen (applyLooks); the
// other rows already show the shifted order. True on Back (the caller opens the parent panel).
template<class Block> bool ApplicationController::layoutKeys(uint32_t keys, ui::ArrangementProperties<Block> &editor,
                                                             BlockOrder<Block> &field, uint32_t now) {
    if (!keys) return false;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = navigator_.keys(keys, editor);
    if (outcome == Outcome::Back) return true;
    if (outcome == Outcome::Changed && editor.order() != field) { field = editor.order(); changed(now); applyLooks(); }
    view_.properties(editor, navigator_);
    return false;
}
void ApplicationController::scopeLayoutKeys(uint32_t keys, uint32_t now) {
    if (layoutKeys(keys, scopeLayout_, prefs_.scope.arrangement, now)) openAnalyze(ui::ScopeActions::Layout);
}
void ApplicationController::openGeneratorLayout(bool fromSettings) {
    generatorLayoutFromSettings_ = fromSettings;
    state_.settingsOpen = false;
    generatorLayout_ = ui::ArrangementProperties<GeneratorBlock>("GENERATOR LAYOUT", prefs_.generatorArrangement);
    openPanel(Panel::GeneratorLayout, generatorLayout_);
}
void ApplicationController::generatorLayoutKeys(uint32_t keys, uint32_t now) {
    if (!layoutKeys(keys, generatorLayout_, prefs_.generatorArrangement, now)) return;
    if (generatorLayoutFromSettings_) { // Back to its row in Settings / Display.
        panel_ = Panel::None; state_.settingsOpen = true;
        navigator_.reset(settingsMenu_.selected());
        showSettings();
        return;
    }
    generatorEditor_.begin(data_.generator().settings(), false, data_.generator().snapshot().timerHz); // Back to its row in PARAMETERS.
    openPanel(Panel::Generator, generatorEditor_);
    for (unsigned i = 0; i < generatorEditor_.count(); ++i)
        if (generatorEditor_.row(i) == ui::GeneratorEditor::Row::Layout) navigator_.reset(i);
    view_.properties(generatorEditor_, navigator_);
}
// The DMM's LAYOUT edits the order of the face shown when it opened (digital or analog).
void ApplicationController::openMeterLayout() {
    meterLayoutAnalog_ = prefs_.meter.analog();
    if (meterLayoutAnalog_) {
        analogLayout_ = ui::ArrangementProperties<AnalogBlock>("ANALOG LAYOUT", prefs_.meter.analogArrangement);
        openPanel(Panel::MeterLayout, analogLayout_);
    } else {
        digitalLayout_ = ui::ArrangementProperties<DigitalBlock>("DIGITAL LAYOUT", prefs_.meter.digitalArrangement);
        openPanel(Panel::MeterLayout, digitalLayout_);
    }
}
void ApplicationController::meterLayoutKeys(uint32_t keys, uint32_t now) {
    const bool back = meterLayoutAnalog_ ? layoutKeys(keys, analogLayout_, prefs_.meter.analogArrangement, now)
                                         : layoutKeys(keys, digitalLayout_, prefs_.meter.digitalArrangement, now);
    if (!back) return;
    meterEditor_ = meterProperties();
    openPanel(Panel::Meter, meterEditor_, ui::MeterProperties::Layout);
}
// COLORS: a committed scheme, colour source, R/G/B edit (MENU) or reset is saved and repaints the
// Scope (view only); Element only selects what the rows below edit; F4 leaves a field unchanged.
void ApplicationController::scopeColorsKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = navigator_.keys(keys, colorsEditor_);
    if (outcome == Outcome::Back) { openAnalyze(ui::ScopeActions::Colors); return; }
    if (outcome == Outcome::Activate) { // A reset may drop the R/G/B rows: the action keeps its selection at its new index.
        const auto action = colorsEditor_.at(navigator_.row());
        colorsEditor_.reset(action);
        navigator_.reset(colorsEditor_.index(action));
    }
    if ((outcome == Outcome::Changed || outcome == Outcome::Activate) && colorsEditor_.appearance() != prefs_.scope.appearance) {
        prefs_.scope.appearance = colorsEditor_.appearance();
        changed(now); applyLooks();
    }
    view_.properties(colorsEditor_, navigator_);
}
} // namespace openosc
