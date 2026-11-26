#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/generator/capabilities.hpp"

namespace openosc::ui {
// The generator's WAVEFORM and PARAMETERS panels. Their rows come from the board's generator
// (generator::Capabilities): ZT703S levels and DAC codes, ZT706S module amplitude in volts.
class GeneratorEditor final : public PropertyList {
public:
    // File: the AWG file list (MENU opens it, docs/AWG.md).
    enum class Row : uint8_t { Shape, Duty, Level, Amplitude, MinimumCode, MaximumCode, Preset, Frequency, Drive, Layout, File };
    // The loaded Arbitrary waveform as the panel shows it (generator::Snapshot) and the file it came
    // from (empty: none, or not a file).
    struct Arbitrary {
        const uint16_t *samples = nullptr;
        unsigned points = 0;
        uint32_t revision = 0;
        const char *file = "";
    };
    // ZT703S presets: main OUTPUT's frequencies, then its reference (TIM13 1 kHz square, 50 %).
    static constexpr uint32_t Presets[] = {10, 50, 100, 500, 1000, 2000, 5000};
    static constexpr unsigned PresetCount = sizeof Presets / sizeof *Presets, Reference = PresetCount;
    // `timerHz`: the DAC timer clock the frequency limit is computed at (generator::Snapshot::timerHz;
    // 0: the highest of any run mode).
    void begin(const generator::Settings &settings, bool wave, uint32_t timerHz = 0) {
        draft_ = {settings, false, false}; wave_ = wave; timerHz_ = timerHz;
    }
    void arbitrary(const Arbitrary &loaded) { arbitrary_ = loaded; }
    const uint16_t *previewSamples(unsigned &points, uint32_t &revision) const override {
        points = arbitrary_.points; revision = arbitrary_.revision; return arbitrary_.samples;
    }
    const generator::Settings &settings() const { return draft_.settings; }
    bool wave() const { return wave_; }
    const char *title() const override { return wave_ ? "WAVEFORM" : "PARAMETERS"; }
    const generator::Settings *preview() const override { return &draft_.settings; }
    const char *notice() const override {
        return draft_.clamped ? "Frequency clamped to shape maximum" : draft_.amplitudeClamped ? "Amplitude clamped to shape limit" : nullptr;
    }
    Row row(unsigned index) const;
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    void adjust(unsigned row, int direction) override;
    int32_t number(unsigned row) const override;
    // A number step may also lower the frequency or amplitude and turn the level Custom: F4 brings
    // back the whole draft as it was when the edit began, with its notice; MENU commits whatever of
    // it changed, together.
    void beginEdit(unsigned) override { checkpoint_ = draft_; }
    bool edited(unsigned, int32_t) const override { return !draft_.settings.same(checkpoint_.settings); }
    void cancelEdit(unsigned, int32_t) override { draft_ = checkpoint_; }
    unsigned count() const override;
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
private:
    void limit(); // Frequency and amplitude within what the shape allows.
    struct Draft {
        generator::Settings settings;
        bool clamped = false, amplitudeClamped = false; // What limit() lowered (the notice).
    };
    Draft draft_, checkpoint_;
    bool wave_ = true;
    uint32_t timerHz_ = 0;
    Arbitrary arbitrary_;
};
} // namespace openosc::ui
