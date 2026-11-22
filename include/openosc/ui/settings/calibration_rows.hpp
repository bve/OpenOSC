#pragma once
#include "openosc/ui/settings/menu.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// What the user sets up on the Gain and Interleave pages: the cell and the reference volts at the
// probe tip for a gain measurement, the interleave offset to apply (tenths of a code).
struct CalibrationDraft {
    unsigned channel = 0, range = 3; // CH1, 100 mV/div: a 3.3 V reference through a 10:1 probe spans ~88 codes.
    int32_t referenceMillivolts = 3300;
    unsigned referenceStep = 2; // CalibrationRows::ReferenceSteps: 100 mV (3.3 V -> 5 V in 17 steps).
    int32_t interleaveTenths = 0;
};
// What the instrument reports for them (DataAccess), computed by the controller for each showing.
struct CalibrationStatus {
    uint32_t probeNumerator = 1, probeDenominator = 1; // The draft channel's probe.
    bool spanKnown = false;
    double span = 0, gainInForce = 1;                 // Codes the reference should give; the draft cell's factor.
    float interleaveInForce = 0, interleaveSaved = 0;
    bool interleaveSavedEnabled = false;
    // Each channel's factory scale gain in hundredths (the record's word, 120 without one; 0: none). In
    // the struct's tail padding: the Settings frames on the key chain (stack budget) do not grow.
    uint16_t factoryGainHundredths[2]{};
};
// The Settings rows of those pages (SettingsProperties hands them over): Choice channel and range,
// Number reference (by the Choice of its step: 1/10/100 mV, 1/10 V; 1 mV resolution, either sign)
// and interleave offset (0.1 code), both shown as text, Info for the rest.
class CalibrationRows {
public:
    static constexpr int32_t ReferenceLimitMillivolts = 400000;
    static constexpr int32_t ReferenceSteps[] = {1, 10, 100, 1000, 10000}; // mV
    static constexpr unsigned ReferenceStepCount = sizeof ReferenceSteps / sizeof *ReferenceSteps;
    CalibrationRows(const CalibrationDraft &draft, const CalibrationStatus &status) : draft_(draft), status_(status) {}
    static bool handles(SettingsAction action);
    const CalibrationDraft &draft() const { return draft_; }
    PropertyKind kind(SettingsAction action) const;
    void value(SettingsAction action, char *output, size_t capacity) const;
    unsigned options(SettingsAction action) const;
    unsigned option(SettingsAction action) const;
    void optionName(SettingsAction action, unsigned index, char *output, size_t capacity) const;
    bool choose(SettingsAction action, unsigned index);
    void adjust(SettingsAction action, int direction);
    int32_t number(SettingsAction action) const;
    void setNumber(SettingsAction action, int32_t value);
private:
    static bool limits(SettingsAction action, int32_t &minimum, int32_t &maximum);
    CalibrationDraft draft_;
    CalibrationStatus status_;
};
} // namespace openosc::ui
