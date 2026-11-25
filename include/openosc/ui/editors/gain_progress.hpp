#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/calibration/gain_calibration.hpp"

namespace openosc::ui {
// The gain measurement while it runs (ZeroOperation Applying/Collecting): what is measured, the
// step, and the user's actions of that step: Retry ground, Reference applied, Save or Cancel.
// Review shows the measured span, the reference and the correction before Save.
class GainProgress final : public PropertyList {
public:
    enum class Action : uint8_t { None, Proceed, Save, Cancel };
    explicit GainProgress(const calibration::GainCalibration &gain);
    const char *title() const override { return gain_.title(); }
    const char *notice() const override { return notice_; }
    unsigned count() const override;
    PropertyKind kind(unsigned row) const override { return action(row) == Action::None ? PropertyKind::Info : PropertyKind::Action; }
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    Action action(unsigned row) const;
    unsigned defaultRow() const; // The step's main action.
private:
    const calibration::GainCalibration &gain_;
    char notice_[48];
};
} // namespace openosc::ui
