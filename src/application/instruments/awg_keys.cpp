#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include <cstdio>
#include <new>
#include <type_traits>

// GEN > Waveform > AWG file (ZT703S, GitHub #175, docs/AWG.md): the /openosc/AWG list, and the chosen
// CSV read a slice a main-loop pass into the generator's stage (the waveform in force stays until the
// whole file is validated), then committed. Up/Down select, MENU (or Right, F1) loads, F4 back to
// WAVEFORM; MODE leaves as anywhere (the browser and an unfinished load go, serviceAwgFiles).
namespace openosc {
void ApplicationController::beginGeneratorEditor(bool wave) {
    const auto &output = data_.generator();
    const auto snapshot = output.snapshot();
    generatorEditor_.begin(output.settings(), wave, snapshot.timerHz);
    const bool file = snapshot.arbitrarySource == generator::ArbitrarySource::File && snapshot.arbitraryRevision == awgRevision_;
    generatorEditor_.arbitrary({snapshot.arbitrarySamples, snapshot.arbitraryPoints, snapshot.arbitraryRevision, file ? awgFile_ : ""});
}
void ApplicationController::openAwgFiles(uint32_t now) {
    if (!awgFiles_) {
        void *memory = lv_malloc(sizeof(AwgFiles));
        if (!memory) { view_.notify(Tooltip::Critical, "Not enough memory for the file list", now); return; }
        awgFiles_ = new (memory) AwgFiles(fs_);
    }
    panel_ = Panel::AwgFiles;
    if (!awgFiles_->refresh()) view_.notify(Tooltip::Critical, awgFiles_->error(), now);
    showAwgFiles();
    refreshSoftKeys();
}
void ApplicationController::closeAwgFiles(bool toPanel) {
    if (awgFiles_) {
        data_.generator().abortArbitrary(awgFiles_); // An unfinished load: the waveform in force stays.
        awgFiles_->~AwgFiles();                       // Closes its file.
        lv_free(awgFiles_);
        awgFiles_ = nullptr;
    }
    if (!toPanel) return;
    view_.focusParameter(0);
    beginGeneratorEditor(true);
    unsigned row = 0;
    while (row + 1 < generatorEditor_.count() && generatorEditor_.row(row) != ui::GeneratorEditor::Row::File) ++row;
    openPanel(Panel::Generator, generatorEditor_, row);
    refreshSoftKeys();
}
void ApplicationController::showAwgFiles() {
    if (awgFiles_ && uiReady_) view_.awgFiles(awgFiles_->list());
}
void ApplicationController::awgFileKeys(uint32_t keys, uint32_t now) {
    if (!keys || !awgFiles_) return;
    if (keys & Keypad::F4) { closeAwgFiles(true); return; } // Also cancels a load in progress.
    if (awgFiles_->reading()) return;
    if (keys & (Keypad::Up | Keypad::Down)) {
        if (!awgFiles_->move(keys & Keypad::Down ? 1 : -1) && awgFiles_->error()) view_.notify(Tooltip::Critical, awgFiles_->error(), now);
        showAwgFiles();
        return;
    }
    if ((keys & (Keypad::Menu | Keypad::Right | Keypad::F1)) && awgFiles_->count()) { loadAwgFile(now); refreshSoftKeys(); }
}
void ApplicationController::loadAwgFile(uint32_t now) {
    using Stage = generator::ArbitraryBanks::Stage;
    auto &output = data_.generator();
    if (data_.zeroBusy() || bodeOwns()) { view_.notify(Tooltip::Unavailable, "Generator in use", now); return; }
    switch (output.beginArbitrary(awgFiles_)) {
    case Stage::Ready: break;
    case Stage::Busy: view_.notify(Tooltip::Unavailable, "AWG memory in use over USB", now); return;
    case Stage::Conflict: view_.notify(Tooltip::Unavailable, "Turn the output off or select AWG first", now); return;
    default: view_.notify(Tooltip::Unavailable, "No AWG on this generator", now); return;
    }
    if (!awgFiles_->open()) { output.abortArbitrary(awgFiles_); view_.notify(Tooltip::Critical, awgFiles_->error(), now); return; }
    char text[80];
    std::snprintf(text, sizeof text, "Loading %s...", awgFiles_->name());
    view_.notify(Tooltip::Progress, text, now);
}
void ApplicationController::serviceAwgFiles(uint32_t now) {
    if (!awgFiles_) return;
    if (panel_ != Panel::AwgFiles) { closeAwgFiles(false); return; } // MODE, Remote, a shutdown, any other path.
    if (!awgFiles_->reading()) return;
    auto &output = data_.generator();
    generator::OutputStage<std::remove_reference_t<decltype(output)>> stage(output, awgFiles_);
    const auto step = awgFiles_->read(stage);
    if (step == AwgFiles::Step::More) return;
    char text[96];
    if (step == AwgFiles::Step::Failed) {
        output.abortArbitrary(awgFiles_);
        view_.notify(Tooltip::Critical, awgFiles_->error(), now);
        refreshSoftKeys();
        return;
    }
    const unsigned points = awgFiles_->points();
    if (data_.zeroBusy() || bodeOwns() || !commitArbitrary(awgFiles_, points, generator::ArbitrarySource::File, now)) {
        output.abortArbitrary(awgFiles_);
        view_.notify(Tooltip::Critical, "Not loaded: waveform memory or rate unavailable", now);
        refreshSoftKeys();
        return;
    }
    std::snprintf(awgFile_, sizeof awgFile_, "%s", awgFiles_->name());
    awgRevision_ = output.snapshot().arbitraryRevision;
    std::snprintf(text, sizeof text, "Loaded %s: %u points", awgFile_, points);
    view_.notify(Tooltip::Done, text, now);
    closeAwgFiles(true);
}
ui::SoftKeys ApplicationController::awgSoftKeys() const {
    ui::SoftKeys keys;
    keys.keys[3] = {"Back", "Waveform", true};
    if (awgFiles_ && awgFiles_->count() && !awgFiles_->reading()) keys.keys[0] = {"Load", "MENU", true};
    return keys;
}
} // namespace openosc
