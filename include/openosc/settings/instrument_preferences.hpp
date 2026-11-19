#pragma once
#include "openosc/measurement/meter_function.hpp"
#include "openosc/measurement/dmm/hold.hpp"
#include <cstdint>
#include "openosc/acquisition/readouts.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/settings/meter_appearance.hpp"
#include "openosc/settings/scope_appearance.hpp"
#include "openosc/settings/screen_arrangement.hpp"
#include "openosc/decode/settings.hpp"

namespace openosc {
struct ScopePreferences {
    uint32_t recordPoints = scope::RecordBudget::Default; // Total of the enabled channels.
    uint32_t enabled[2] = {1, 1}, coupling[2] = {0, 0}, position[2] = {80, 80};
    uint32_t probeNumerator[2] = {1, 1}, probeDenominator[2] = {1, 1};
    uint32_t triggerSource = 0, triggerEdge = 0, triggerMode = 0;
    int32_t triggerLevel = 0;
    uint32_t triggerPosition=250;
    uint32_t triggerReject=0; // TriggerSettings::reject; files before it: off.
    scope::ReadoutSelection measurements;
    uint32_t runMode = 0; // Index into RunModes.
    uint32_t rollAuto=1;
    // ZT703S: Interleave CH1 (Settings > Device > Performance): 1 lets CH1 alone at 10/25 ns run through
    // both ADCs (InterleaveRule). Off by default and in files before it; ignored on the ZT706S, whose
    // factory interleave needs no choice. A run-mode change never alters it.
    uint32_t interleave=0;
    // A service choice (FactoryRecordPolicy: 0 auto, 1 long, 2 compact) for reading the factory record
    // at the NEXT start (DataAccess::initialize); Reload settings never re-reads the record. Only on
    // outside evidence about an ambiguous record (docs/ZERO_CALIBRATION.md); files without it: auto.
    uint32_t factoryRecordFormat=0;
    // The look only (never the capture, HOLD or the measurements): the blocks' order under the
    // status bar, and the colour scheme with its per-role overrides. Files before them: defaults.
    ScopeArrangement arrangement;
    ScopeAppearance appearance;
    // The protocol decoder (docs/SCOPE_PROTOCOL_DECODERS_PLAN.md): off by default; files before it, or
    // an unreadable "decoder" object: the defaults, the rest of the file as it is.
    decoding::Settings decoder;
    bool valid() const;
};
struct MeterPreferences {
    // display: 0 primary, 1 statistics (MEAN), 2 trend, 3 analog dial (appended: 0..2 keep their meaning).
    static constexpr uint32_t Displays = 4, Faces = 3, Palettes = 3, AnalogDisplay = 3;
    uint32_t display = 0, statistics = 1, automatic = 1, range = 0;
    uint32_t face = 0, palette = 0; // ui::MeterStyles: IBM/Mono/Segment, Dark/Amber/Paper.
    // The function (MeterFunctions index) the manual `range` was confirmed in; MeterFunctionCount:
    // unknown (files before it) - such a manual range is not restored, Auto still is.
    uint32_t rangeFunction = MeterFunctionCount;
    // Per-element overrides over palette and face (Measurement > Appearance); none: the base look.
    // The reading's font override, when set, is the reading's face; Default: `face`.
    MeterAppearance appearance;
    // The blocks' order under the status bar: the digital faces (display 0..2) and the analog dial
    // (display 3) each keep their own. Files before them: defaults (soft keys at the bottom).
    DigitalArrangement digitalArrangement;
    AnalogArrangement analogArrangement;
    // How HOLD takes its reading (Measurement > Hold): Manual by default and in files before it. Only
    // the mode and its parameters are kept: a cycle or a snapshot never outlives a restart.
    dmm::HoldSettings hold;
    bool analog() const { return display == AnalogDisplay; } // Which of the two orders shows.
    bool valid() const {
        return display < Displays && statistics <= 1 && automatic <= 1 && range < 8 && face < Faces && palette < Palettes &&
               rangeFunction <= MeterFunctionCount && appearance.valid() && digitalArrangement.valid() && analogArrangement.valid() && hold.valid();
    }
};
} // namespace openosc
