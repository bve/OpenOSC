#pragma once
#include "openosc/measurement/dmm/hold.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// Measurement > Hold: how the HOLD key takes its reading (dmm::HoldSettings, docs/DMM_AUTO_HOLD.md).
// Mode a choice, the parameters numbers within HoldSettings' own limits and steps (the one check of
// the settings file and SCPI too); the band and the rearm threshold stay a pair (rearm > band): one
// moved onto the other carries it along, and cancelling an edit gives both back. The controller
// takes each committed field at once (saved, applied: a running cycle ends, none starts).
class MeterHoldProperties final : public PropertyList {
public:
    enum Row { Mode, Delay, StableTime, Band, Rearm, Readings, Zero, Count };
    explicit MeterHoldProperties(const dmm::HoldSettings &settings = {}) : settings_(settings), saved_(settings) {}
    const dmm::HoldSettings &settings() const { return settings_; }
    const char *title() const override { return "HOLD"; }
    const char *notice() const override;
    unsigned count() const override { return Count; }
    PropertyKind kind(unsigned row) const override { return row == Mode ? PropertyKind::Choice : PropertyKind::Number; }
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override { return row == Mode ? unsigned(dmm::HoldMode::Count) : 0; }
    unsigned option(unsigned row) const override { return row == Mode ? unsigned(settings_.mode) : 0; }
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    // Delay in seconds, stable time in 100 ms steps, the rest in units; Up/Down a coarse step. The
    // rows show their values as text ("6 s", "20 digits"), no slider: range() stays the default.
    void adjust(unsigned row, int steps) override;
    int coarseStep(unsigned row) const override;
    int32_t number(unsigned row) const override;
    void setNumber(unsigned row, int32_t value) override;
    void beginEdit(unsigned) override { saved_ = settings_; }
    void cancelEdit(unsigned, int32_t) override { settings_ = saved_; }
    bool edited(unsigned, int32_t) const override { return settings_ != saved_; }

    static bool limits(unsigned row, int32_t &low, int32_t &high); // In number()'s units.

private:
    dmm::HoldSettings settings_, saved_; // saved_: at the edit's start (the pair moves together).
};
} // namespace openosc::ui
