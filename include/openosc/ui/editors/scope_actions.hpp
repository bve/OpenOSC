#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/ui/scope/options.hpp"
#include <cstdio>

namespace openosc::ui {
// ANALYZE: panels to open (Measurements, Display, Layout, Colors, Acquisition, Decoder), actions, and the cursor lines
// (a choice: Off, Time, Level, Time+Level), taken at once.
class ScopeActions final : public PropertyList {
public:
    enum Action { Display, Measurements, Cursors, Layout, Colors, Acquisition, Decoder, ArmSingle, SaveRaw, SaveCsv, Spectrum, CenterChannels, ResetPan, TriggerMidpoint, Count };
    explicit ScopeActions(const plot::Options &options = {}) : cursors_(options.cursors),
          spectrum_(options.mode == plot::Mode::Spectrum || options.mode == plot::Mode::SpectrumOverlay) {}
    const char *title() const override { return "ANALYZE"; }
    unsigned count() const override { return Count; }
    PropertyKind kind(unsigned row) const override {
        return row == Cursors ? PropertyKind::Choice : row == Measurements || row == Display || row == Layout || row == Colors || row == Acquisition || row == Decoder ? PropertyKind::Page : PropertyKind::Action;
    }
    void name(unsigned row, char *output, size_t capacity) const override {
        static constexpr const char *names[] = {"Display", "Measurements", "Cursors", "Layout", "Colors", "Acquisition", "Decoder",
            "Rearm Single", "Save RAW", "Save CSV","FFT spectrum", "Center channels", "Reset pan", "Trigger 50%"};
        std::snprintf(output, capacity, "%s", row == Spectrum && spectrum_ ? "FFT settings" : row < Count ? names[row] : "");
    }
    unsigned options(unsigned row) const override { return row == Cursors ? 4 : 0; }
    unsigned option(unsigned row) const override { return row == Cursors ? unsigned(cursors_.time) | unsigned(cursors_.voltage) << 1 : 0; }
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override {
        if (row != Cursors) { if (capacity) output[0] = 0; return; }
        const char *time = spectrum_ ? "Frequency" : "Time";
        static constexpr const char *level = "Level";
        if (index == 0) std::snprintf(output, capacity, "Off");
        else if (index == 3) std::snprintf(output, capacity, "%s+%s", time, level);
        else std::snprintf(output, capacity, "%s", index == 1 ? time : level);
    }
    bool choose(unsigned row, unsigned index) override {
        if (row != Cursors || index > 3) return false;
        cursors_.time = index & 1; cursors_.voltage = index & 2;
        return true;
    }
    const plot::CursorOptions &cursors() const { return cursors_; }
private:
    plot::CursorOptions cursors_;
    bool spectrum_ = false; // An FFT view: the Spectrum row opens its fields.
};
} // namespace openosc::ui
