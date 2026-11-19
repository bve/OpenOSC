#pragma once
#include "openosc/calibration/data.hpp"
#include "openosc/acquisition/frame.hpp"

namespace openosc::calibration {
// The ZT706S service interleave offset: the codes added to the HIGH ADC's points of an interleaved
// CH1 record, in place of the factory record's float (CalibrationData::interleaveCorrection) while
// enabled. Set in StepCodes; a value beyond LimitCodes is no offset between two working ADCs. Only
// a record's own metadata may carry it (never a live value read at display time); a reader applies
// it as one whole code (scope::InterleaveCorrection::shift), so tenths only choose the rounding.
// The ZT703S keeps one offset per timing instead (`clocked`): measured by the user in the real
// opposed-clock interleave of that run mode (InterleaveOffsetMeasurement), keyed by the core clock in
// MHz AND its ADC divider (the ADC clock), so a profile slot holding another clock or divider never
// takes it; a timing without one interleaves with 0 (uncalibrated). Entries of other timings stay.
struct InterleaveProfile {
    static constexpr float LimitCodes = scope::InterleaveCorrection::Limit, StepCodes = .1f;
    static constexpr unsigned Clocks = 4; // Room for every run mode and a stale one or more.
    struct Clocked { uint16_t coreMhz = 0; uint8_t adcDivider = 0; float offset = 0; };
    uint16_t factoryCrc = 0;
    bool enabled = false;
    float offset = 0;
    Clocked clocked[Clocks]{};
    uint8_t clockedCount = 0;
    static bool allowed(float codes) { return scope::InterleaveCorrection::valid(codes); } // NaN: no.
    static constexpr bool timing(uint32_t coreHz, uint32_t divider) {
        return coreHz && !(coreHz % 1000000U) && coreHz / 1000000U <= 0xffffU && divider >= 1 && divider <= 0xffU;
    }
    bool valid(const CalibrationData &factory) const;
    // The ZT703S offset measured at this timing, or null (not measured: uncalibrated).
    const Clocked *at(uint32_t coreHz, uint32_t divider) const {
        for (unsigned i = 0; i < clockedCount; ++i)
            if (uint32_t(clocked[i].coreMhz) * 1000000U == coreHz && clocked[i].adcDivider == divider) return &clocked[i];
        return nullptr;
    }
    // Sets (or replaces) the one of this timing; the others stay. When every slot holds another timing,
    // the oldest of those no run mode uses any more (`current` says which are) gives its place; false
    // when invalid or none is free.
    template <class Current> bool measured(uint32_t coreHz, uint32_t divider, float codes, Current current) {
        if (!timing(coreHz, divider) || !allowed(codes)) return false;
        const Clocked entry{uint16_t(coreHz / 1000000U), uint8_t(divider), codes};
        for (unsigned i = 0; i < clockedCount; ++i)
            if (clocked[i].coreMhz == entry.coreMhz && clocked[i].adcDivider == entry.adcDivider) { clocked[i] = entry; return true; }
        if (clockedCount < Clocks) { clocked[clockedCount++] = entry; return true; }
        for (unsigned i = 0; i < clockedCount; ++i)
            if (!current(uint32_t(clocked[i].coreMhz) * 1000000U, clocked[i].adcDivider)) { clocked[i] = entry; return true; }
        return false;
    }
    bool measured(uint32_t coreHz, uint32_t divider, float codes) {
        return measured(coreHz, divider, codes, [](uint32_t, uint32_t) { return true; });
    }
};
} // namespace openosc::calibration
