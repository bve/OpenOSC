#include "openosc/ui/editors/gain_progress.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/data/decimal.hpp"
#include <cmath>
#include <cstdio>

namespace openosc::ui {
namespace {
using Step = calibration::GainCalibration::Step;
void codes(double value, char *output, size_t capacity) {
    char number[16];
    Decimal::fixed(value, 2, number, sizeof number);
    std::snprintf(output, capacity, "%s codes", number);
}
// The reference as entered (whole millivolts, CalibrationDraft).
int32_t millivolts(double volts) { return int32_t(std::lround(volts * 1000)); }
void factor(double value, char *output, size_t capacity) {
    char number[16];
    Decimal::fixed(value, 4, number, sizeof number);
    std::snprintf(output, capacity, "x%s", number);
}
}
GainProgress::GainProgress(const calibration::GainCalibration &gain) : gain_(gain) {
    char volts[20];
    plot::Format::exactMillivolts(millivolts(gain.referenceVolts()), volts, sizeof volts);
    if (gain.step() == Step::AwaitReference)
        std::snprintf(notice_, sizeof notice_, "Apply %s to the CH%u tip", volts, gain.channel() + 1);
    else std::snprintf(notice_, sizeof notice_, "%s", gain.requirement());
}
unsigned GainProgress::count() const {
    return gain_.step() == Step::Ground || gain_.step() == Step::Reference ? 4 : 5;
}
GainProgress::Action GainProgress::action(unsigned row) const {
    if (count() == 5 && row == 3) return gain_.step() == Step::Review ? Action::Save : Action::Proceed;
    return row == count() - 1 ? Action::Cancel : Action::None;
}
unsigned GainProgress::defaultRow() const { return 3; }
void GainProgress::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *Measuring[] = {"Cell", "Step", "Reference", "Cancel"};
    static constexpr const char *Retry[] = {"Cell", "Input", "Reference", "Retry ground", "Cancel"};
    static constexpr const char *Waiting[] = {"Cell", "Ground", "Reference", "Reference applied", "Cancel"};
    static constexpr const char *Review[] = {"Measured span", "Reference", "Correction", "Save", "Cancel"};
    const auto *names = Measuring;
    switch (gain_.step()) {
    case Step::RetryGround: names = Retry; break;
    case Step::AwaitReference: names = Waiting; break;
    case Step::Review: names = Review; break;
    case Step::Ground: case Step::Reference: break;
    }
    std::snprintf(output, capacity, "%s", row < count() ? names[row] : "");
}
void GainProgress::value(unsigned row, char *output, size_t capacity) const {
    if (capacity) *output = 0;
    if (action(row) != Action::None) return;
    char text[24];
    const auto step = gain_.step();
    if (step == Step::Review) {
        if (row == 0) codes(gain_.span(), output, capacity);
        else if (row == 1) plot::Format::exactMillivolts(millivolts(gain_.referenceVolts()), output, capacity);
        else {
            char was[16];
            factor(gain_.proposed(), text, sizeof text); factor(gain_.inForce(), was, sizeof was);
            std::snprintf(output, capacity, "%s (now %s)", text, was);
        }
        return;
    }
    if (row == 0) {
        plot::Format::quantity(scope::VoltageRanges::millivolts(gain_.range()) / 1000.0, "V/div", text, sizeof text);
        std::snprintf(output, capacity, "CH%u %s", gain_.channel() + 1, text);
    } else if (row == 1) {
        if (step == Step::AwaitReference) codes(gain_.groundCode(), output, capacity);
        else if (step == Step::RetryGround) {
            const auto failure = gain_.failure();
            std::snprintf(output, capacity, "%s", failure == calibration::GainCalibration::Failure::Saturated ? "At ADC limit" :
                          failure == calibration::GainCalibration::Failure::Unstable ? "Unstable" : "Outside zero range");
        } else std::snprintf(output, capacity, "%s %u%%", step == Step::Ground ? "Ground" : "Reference", gain_.percent());
    } else if (row == 2) plot::Format::exactMillivolts(millivolts(gain_.referenceVolts()), output, capacity);
}
} // namespace openosc::ui
