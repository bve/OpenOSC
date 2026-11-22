#include "openosc/ui/settings/calibration_rows.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/calibration/interleave_profile.hpp"
#include "openosc/data/decimal.hpp"
#include <cmath>
#include <cstdio>

namespace openosc::ui {
namespace {
using Action = SettingsAction;
constexpr int32_t InterleaveLimitTenths = int32_t(calibration::InterleaveProfile::LimitCodes * 10);
void codes(double value, char *output, size_t capacity) { // "-0.3 codes"
    char number[16];
    Decimal::fixed(value, 1, number, sizeof number);
    std::snprintf(output, capacity, "%s codes", number);
}
}
bool CalibrationRows::handles(SettingsAction action) {
    switch (action) {
    case Action::GainChannel: case Action::GainRange: case Action::GainReference: case Action::GainReferenceStep: case Action::GainProbe:
    case Action::GainSpan: case Action::GainInForce: case Action::GainFactory: case Action::InterleaveOffset: case Action::InterleaveInForce:
    case Action::InterleaveSaved: return true;
    default: return false;
    }
}
PropertyKind CalibrationRows::kind(SettingsAction action) const {
    switch (action) {
    case Action::GainChannel: case Action::GainRange: case Action::GainReferenceStep: return PropertyKind::Choice;
    case Action::GainReference: case Action::InterleaveOffset: return PropertyKind::Number;
    default: return PropertyKind::Info;
    }
}
unsigned CalibrationRows::options(SettingsAction action) const {
    return action == Action::GainChannel ? 2 : action == Action::GainRange ? scope::VoltageRanges::Count
         : action == Action::GainReferenceStep ? ReferenceStepCount : 0;
}
unsigned CalibrationRows::option(SettingsAction action) const {
    return action == Action::GainChannel ? draft_.channel : action == Action::GainRange ? draft_.range
         : action == Action::GainReferenceStep ? draft_.referenceStep : 0;
}
void CalibrationRows::optionName(SettingsAction action, unsigned index, char *output, size_t capacity) const {
    if (action == Action::GainChannel) std::snprintf(output, capacity, "CH%u", index % 2 + 1);
    else if (action == Action::GainRange) plot::Format::quantity(scope::VoltageRanges::millivolts(index) / 1000.0, "V/div", output, capacity);
    else if (action == Action::GainReferenceStep) plot::Format::quantity(ReferenceSteps[index % ReferenceStepCount] / 1000.0, "V", output, capacity);
    else if (capacity) *output = 0;
}
void CalibrationRows::value(SettingsAction action, char *output, size_t capacity) const {
    char number[24];
    switch (action) {
    case Action::GainChannel: case Action::GainRange: case Action::GainReferenceStep: optionName(action, option(action), output, capacity); break;
    case Action::GainReference: plot::Format::exactMillivolts(draft_.referenceMillivolts, output, capacity); break;
    case Action::GainProbe:
        std::snprintf(output, capacity, "%lu:%lu (CH%u menu)", (unsigned long)status_.probeNumerator,
                      (unsigned long)status_.probeDenominator, draft_.channel + 1);
        break;
    case Action::GainSpan:
        if (!status_.spanKnown) { std::snprintf(output, capacity, "No factory scale"); break; }
        std::snprintf(output, capacity, "%ld codes%s", std::lround(status_.span),
                      std::abs(status_.span) < 40 ? " (under 40)" : std::abs(status_.span) > 110 ? " (over 110)" : "");
        break;
    case Action::GainInForce:
        Decimal::fixed(status_.gainInForce, 4, number, sizeof number);
        std::snprintf(output, capacity, "x%s", number);
        break;
    case Action::GainFactory: { // "x1.23 / x1.20": the record's words (GitHub #80), 1 % steps.
        char gains[2][12];
        for (unsigned c = 0; c < 2; ++c) {
            const unsigned hundredths = status_.factoryGainHundredths[c];
            if (hundredths) std::snprintf(gains[c], sizeof gains[c], "x%u.%02u", hundredths / 100, hundredths % 100);
            else std::snprintf(gains[c], sizeof gains[c], "none");
        }
        std::snprintf(output, capacity, "%s / %s", gains[0], gains[1]);
        break;
    }
    case Action::InterleaveOffset: codes(draft_.interleaveTenths / 10.0, output, capacity); break;
    case Action::InterleaveInForce: codes(status_.interleaveInForce, output, capacity); break;
    case Action::InterleaveSaved:
        if (status_.interleaveSavedEnabled) codes(status_.interleaveSaved, output, capacity);
        else std::snprintf(output, capacity, "Factory");
        break;
    default: if (capacity) *output = 0; break;
    }
}
bool CalibrationRows::choose(SettingsAction action, unsigned index) {
    if (index >= options(action)) return false;
    if (action == Action::GainChannel) draft_.channel = index;
    else if (action == Action::GainRange) draft_.range = index;
    else draft_.referenceStep = index;
    return true;
}
// The limits of a Number; shown as text, not as a slider (a reference of +-400 V is set in mV).
bool CalibrationRows::limits(SettingsAction action, int32_t &minimum, int32_t &maximum) {
    if (action == Action::GainReference) { minimum = -ReferenceLimitMillivolts; maximum = ReferenceLimitMillivolts; return true; }
    if (action == Action::InterleaveOffset) { minimum = -InterleaveLimitTenths; maximum = InterleaveLimitTenths; return true; }
    return false;
}
void CalibrationRows::adjust(SettingsAction action, int direction) {
    if (!direction) return;
    int32_t minimum = 0, maximum = 0;
    if (!limits(action, minimum, maximum)) return;
    const int32_t step = action == Action::GainReference ? ReferenceSteps[draft_.referenceStep % ReferenceStepCount] : 1;
    int32_t value = number(action);
    value += direction > 0 ? step : -step;
    setNumber(action, value < minimum ? minimum : value > maximum ? maximum : value);
}
int32_t CalibrationRows::number(SettingsAction action) const {
    return action == Action::GainReference ? draft_.referenceMillivolts : action == Action::InterleaveOffset ? draft_.interleaveTenths : 0;
}
void CalibrationRows::setNumber(SettingsAction action, int32_t value) {
    if (action == Action::GainReference) draft_.referenceMillivolts = value;
    else if (action == Action::InterleaveOffset) draft_.interleaveTenths = value;
}
} // namespace openosc::ui
