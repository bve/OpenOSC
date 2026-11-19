#pragma once
#include "openosc/settings/preferences.hpp"
#include "openosc/ui/settings/menu.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/ui/settings/calibration_rows.hpp"

namespace openosc::ui {
// The rows of the open Settings page as properties: pages, actions, and the settings themselves as
// choices (battery, run mode, record memory, backlight, idle level, auto power off), a toggle (FPS)
// and a number (brightness). Choosing changes a copy of the preferences (preferences()); the
// controller takes what changed and applies it. An old auto power off value stays as it is, shown,
// until another one is chosen.
class SettingsProperties final : public PropertyList {
public:
    // activeRunMode: the profile the core runs (ClockControl::activeMode), RunModeCount for none.
    // calibration: the Gain and Interleave pages' draft and what the instrument reports for them.
    SettingsProperties(const SettingsMenu &menu, const Preferences &preferences, uint32_t activeRunMode,
                       const CalibrationDraft &draft = {}, const CalibrationStatus &status = {})
        : menu_(menu), prefs_(preferences), activeRunMode_(activeRunMode), calibration_(draft, status) {}
    const Preferences &preferences() const { return prefs_; }
    // The interleave as DataAccess reports it: what keeps the request in force from interleaving
    // (InterleaveConditions bits), and the ZT703S offset of the clock that runs (null: not measured).
    struct InterleaveState { uint32_t blockers = 0; bool calibrated = true; float offset = 0; bool measuring = false; };
    void interleave(const InterleaveState &state) { interleave_ = state; }
    const CalibrationDraft &calibration() const { return calibration_.draft(); }
    const char *title() const override { return menu_.title(); }
    unsigned count() const override { return menu_.count(); }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    void adjust(unsigned row, int direction) override;
    int32_t number(unsigned row) const override;
    void setNumber(unsigned row, int32_t value) override;
    bool range(unsigned row, int32_t &minimum, int32_t &maximum) const override;
private:
    SettingsAction action(unsigned row) const { return menu_.entry(row).action; }
    const SettingsMenu &menu_;
    Preferences prefs_;
    InterleaveState interleave_;
    uint32_t activeRunMode_;
    CalibrationRows calibration_;
};
} // namespace openosc::ui
