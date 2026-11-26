#include "openosc/application/controller.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/generator/frequency.hpp"
#include "openosc/platform/input.hpp"

namespace openosc {
// A committed WAVEFORM/PARAMETERS field: shape, frequency, level, codes and amplitude as the
// editor normalised them, together; refused, the editor shows the generator in force. The output
// switch stays its own action. F1/F2/F3 go to the other panels (switchGeneratorPanel).
void ApplicationController::generatorEditorKeys(uint32_t keys, uint32_t now) {
    generatorPanelKeys(keys, now);
    refreshSoftKeys();
}
void ApplicationController::generatorPanelKeys(uint32_t keys, uint32_t now) {
    if (switchGeneratorPanel(keys, now)) return;
    const auto key = fieldKeys(keys, generatorEditor_, now);
    if (key == FieldKey::Action && generatorEditor_.row(navigator_.row()) == ui::GeneratorEditor::Row::Layout) {
        openGeneratorLayout(false); return;
    }
    if (key == FieldKey::Action && generatorEditor_.row(navigator_.row()) == ui::GeneratorEditor::Row::File) {
        openAwgFiles(now); return;
    }
    if (key != FieldKey::Committed) return;
    if (!applyGenerator(generatorEditor_.settings(), now)) view_.notify(Tooltip::Unavailable, "Unsupported waveform/frequency/duty", now);
    beginGeneratorEditor(generatorEditor_.wave());
    view_.properties(generatorEditor_, navigator_);
}
void ApplicationController::openGeneratorEditor(bool wave) {
    view_.focusParameter(wave ? 0 : 2);
    beginGeneratorEditor(wave);
    openPanel(Panel::Generator, generatorEditor_);
    refreshSoftKeys();
}
void ApplicationController::openFrequencyEditor() {
    const auto &settings = data_.generator().settings();
    view_.focusParameter(1);
    using Tuning = generator::Tuning;
    frequencyEditor_.begin(Tuning::value(settings), Tuning::maximum(settings, data_.generator().snapshot().timerHz), Tuning::minimum(settings),
                           settings.arbitrary());
    panel_ = Panel::Frequency;
    view_.frequencyEditor(frequencyEditor_);
    refreshSoftKeys();
}
// The field in progress is applied as on leaving it (MENU): a number's draft, the option a list
// shows, the frequency being edited. Refused, nothing changes, the draft stays on show and the panel
// stays open with a notice: nothing is dropped or applied silently.
bool ApplicationController::finishGeneratorField(uint32_t now) {
    if (panel_ == Panel::Frequency) {
        auto settings = data_.generator().settings();
        const auto before = settings;
        generator::Tuning::set(settings, frequencyEditor_.value());
        if (settings.same(before)) return true;
        if (applyGenerator(settings, now)) return true;
        view_.notify(Tooltip::Unavailable, settings.arbitrary() ? "Not applied: unsupported sample rate" : "Not applied: unsupported output frequency", now);
        view_.frequencyEditor(frequencyEditor_);
        return false;
    }
    // The field still open is finished on copies: only an accepted result changes anything. Refused,
    // the panel keeps its list, its open field (Choice cursor or Number draft, F4 still cancels it)
    // and draft exactly as they were, so the same switch can be tried again.
    auto editor = generatorEditor_;
    auto navigator = navigator_;
    if (navigator.mode() != ui::PropertyNavigator::Mode::Browse &&
        navigator.keys(Keypad::Menu, editor) == ui::PropertyNavigator::Outcome::Refused) {
        view_.notify(Tooltip::Unavailable, editor.notice() ? editor.notice() : "Not applied", now);
        return false;
    }
    if (editor.settings().same(data_.generator().settings()) || applyGenerator(editor.settings(), now)) return true;
    view_.notify(Tooltip::Unavailable, "Not applied: unsupported waveform/frequency/duty", now);
    return false;
}
bool ApplicationController::switchGeneratorPanel(uint32_t keys, uint32_t now) {
    // In FREQUENCY, F2 is its unit key (Hz/kHz/MHz): only F1 and F3 leave it.
    const uint32_t switches = keys & (Keypad::F1 | Keypad::F3 | (panel_ == Panel::Frequency ? 0U : uint32_t(Keypad::F2)));
    if (!switches) return false;
    const bool frequency = switches & Keypad::F2, wave = !frequency && (switches & Keypad::F1);
    const bool there = frequency ? panel_ == Panel::Frequency
                                 : panel_ == Panel::Generator && generatorEditor_.wave() == wave;
    if (there || !finishGeneratorField(now)) return true; // Already there, or refused: stays.
    if (frequency) openFrequencyEditor();
    else openGeneratorEditor(wave);
    return true;
}
} // namespace openosc
