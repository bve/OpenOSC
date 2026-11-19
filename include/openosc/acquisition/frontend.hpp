#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/calibration/scope_calibration.hpp"
#include "openosc/acquisition/ranges.hpp"

namespace openosc::scope {
struct FrontendSettings {
    unsigned range[2] = {5, 5};
    int position[2] = {80, 80};
    Coupling coupling[2] = {Coupling::Dc, Coupling::Dc};
    bool enabled[2] = {true, true};
    // Interleave of CH1 (ZT706S factory, ZT703S selectable; CH1 alone through both ADCs, InterleavedWords):
    // control bits 8 and 11.
    bool interleave = false;
    // The factory's interleave range correction (0804F3F0, only while interleaved): CH1's relays and
    // offset at the next range index (at most 10) while `range` stays the user's setting.
    bool rangeBoost = false;
#ifdef OPENOSC_INTERLEAVE_PROBE
    // Diagnostic probe only (InterleaveProbe SharedInput): the selector bits 8/11 with both channels and
    // in-phase ADC clocks; the capture's layout and clocks follow `interleave` alone.
    bool sharedInput = false;
#endif
    // The range the hardware of a channel is set to.
    unsigned physicalRange(unsigned c) const { return c == 0 && rangeBoost && range[0] < 10 ? range[0] + 1 : range[c]; }
    // Interleaving needs CH1 alone; the boost exists only within it.
    bool consistent() const {
#ifdef OPENOSC_INTERLEAVE_PROBE
        if (sharedInput && (interleave || rangeBoost || !enabled[0] || !enabled[1])) return false;
#endif
        return interleave ? enabled[0] && !enabled[1] : !rangeBoost;
    }
    // Whether the selector bits (8/11) are set: an interleave, or the probe's shared input.
    bool selector() const {
#ifdef OPENOSC_INTERLEAVE_PROBE
        if (sharedInput) return true;
#endif
        return interleave;
    }
};
class AnalogFrontend {
public:
    // A change of the analog path (relays, coupling, the filtered TIM8 offset PWM) needs this long
    // before the ADC sees the input at the new settings; a capture that starts earlier records the
    // transition. The same hold as the zero calibration's; not yet measured on the instrument.
    static constexpr uint32_t SettleMs = 50;
    static constexpr uint16_t InterleaveBits = 0x0900; // Control word bits 8 and 11 (factory 0804B878).
    // The offset PWM comes from the calibration overlay (factory, hardware-zero profile, trial).
    explicit AnalogFrontend(const calibration::ScopeCalibration &calibration) : calibration_(calibration) {}
    bool scope(const FrontendSettings &settings,int16_t triggerLevel);
    void multimeter(uint16_t generatorBits = 0); // generator::controlBits kept in the DMM word.
    void multimeterOff(); // The DMM chip unpowered, as on MODE to the scope.
    static bool rangeForMillivolts(uint32_t value, unsigned &index) { return VoltageRanges::index(value, index); }
    static uint32_t millivoltsForRange(unsigned index) { return VoltageRanges::millivolts(index); }
    // The next scope() rewrites the whole path and starts the settle even under the same settings:
    // a calibration's fresh settle origin, after its configure (the latch word is the same, so no
    // relay switches).
    void forceChange() { forced_ = true; }
    // Successful scope() calls, a change or not: a request configured earlier is at the hardware
    // once this has moved (a deferred capture start applies it later than its configure).
    uint32_t applies() const { return applies_; }
    // When the analog path last changed (Board::milliseconds): settled SettleMs later.
    uint32_t changedAt() const { return changedAt_; }
    // What is left of the settle time after the last analog change (not a trigger level change).
    uint32_t unsettledMs(uint32_t now) const {
        const uint32_t elapsed = now - changedAt_;
        return settling_ && elapsed < SettleMs ? SettleMs - elapsed : 0;
    }

private:
    void initialize();
    const calibration::ScopeCalibration &calibration_;
    uint32_t generation_ = 0; // Of the compares applied: another one is a change of the analog path.
    bool initialized_ = false;
    bool referenceReady_ = false;
    bool scopeActive_ = false, settling_ = false, forced_ = false;
    uint32_t changedAt_ = 0, applies_ = 0;
    FrontendSettings previous_;
    int16_t previousLevel_=0;
};
} // namespace openosc::scope
