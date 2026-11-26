#pragma once
#include "openosc/measurement/meter_function.hpp"
#include "openosc/measurement/dmm/hold.hpp"
#include "openosc/measurement/dmm/log_settings.hpp"
#include "openosc/measurement/dmm/temperature.hpp"
#include <cstdint>
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/readouts.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/settings/meter_appearance.hpp"
#include "openosc/settings/scope_appearance.hpp"
#include "openosc/settings/screen_arrangement.hpp"
#include "openosc/settings/scope_keys.hpp"
#include "openosc/decode/settings.hpp"
#include "openosc/board/current.hpp"

namespace openosc {
struct ScopePreferences {
    uint32_t recordPoints = scope::RecordBudget::Default; // Total of the enabled channels.
    uint32_t enabled[2] = {1, 1}, coupling[2] = {0, 0}, position[2] = {80, 80};
    uint32_t probeNumerator[2] = {1, 1}, probeDenominator[2] = {1, 1};
    // The qualified triggers (GitHub #133, TriggerSettings): files without them read an edge trigger
    // with these defaults. Keep ADC codes, fractions and small choices at their natural widths.
    uint32_t triggerMinimumNs = scope::TriggerSettings{}.minimumNs, triggerMaximumNs = scope::TriggerSettings{}.maximumNs;
    uint16_t triggerPosition = 250;
    int8_t triggerLevel = 0, triggerLevel2 = scope::TriggerSettings{}.level2;
    uint8_t triggerSource = 0, triggerEdge = 0, triggerMode = 0;
    uint8_t triggerReject = 1; // TriggerSettings::reject; files without it: on.
    uint8_t triggerType = 0, triggerCondition = uint8_t(scope::TriggerCondition::Greater);
    scope::ReadoutSelection measurements;
    uint32_t runMode = 0; // Index into RunModes.
    uint32_t rollAuto=1;
    uint32_t longerRecord = 1;
    // InterleaveMode: Off/On/Auto. Defaults: ZT706S Auto (its factory rule), ZT703S Off (Auto: MKR's rule).
    uint32_t interleave = board::Current::Features.adcInterleave ? 2 : 0;
    // A service choice (FactoryRecordPolicy: 0 auto, 1 long, 2 compact, 3 mkr) for reading the factory record
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
    ScopeKeys keys; // AUTO and the F2/F4 holds on the waveform screen (Settings > Display > Scope shortcuts).
    bool valid() const;
};
struct MeterPreferences {
    // display: 0 primary, 1 statistics (MEAN), 2 trend, 3 analog dial (appended: 0..2 keep their meaning).
    static constexpr uint32_t Displays = 4, Faces = 3, Palettes = 3, AnalogDisplay = 3;
    uint32_t display = 0, statistics = 1, automatic = 1, range = 0, bar = 1; // bar: the 0-25 bar graph.
    uint32_t face = 0, palette = 0; // ui::MeterStyles: IBM/Mono/Segment, Dark/Amber/Paper.
    // The function (MeterFunctions index) the manual `range` was confirmed in; MeterFunctionCount:
    // unknown (files before it) - such a manual range is not restored, Auto still is.
    uint32_t rangeFunction = MeterFunctionCount;
    // Per-element overrides over palette and face (Measurement > Appearance); none: the base look.
    // The reading's font override, when set, is the reading's face; Default: `face`.
    MeterAppearance appearance;
    // Measurement > Session log (GitHub #144): the next session's mode, interval, duration and event
    // band, in the two bytes after `appearance` (dmm::LogSettings). Defaults in files before it; a
    // session itself never outlives a restart.
    dmm::LogSettings log;
    // Measurement > Appearance > Preset (GitHub #68). face, palette and appearance above are always the
    // user's one Custom look; Hybrid shows MeterAppearanceCatalog's constant instead and keeps them, so
    // Custom brings them back. A manual edit while Hybrid shows starts a Custom look from it (custom()).
    static constexpr uint32_t Presets = 2, CustomPreset = 0, HybridPreset = 1;
    uint32_t preset = CustomPreset;
    // The blocks' order under the status bar: the digital faces (display 0..2) and the analog dial
    // (display 3) each keep their own. Files before them: defaults (soft keys at the bottom).
    DigitalArrangement digitalArrangement;
    AnalogArrangement analogArrangement;
    bool barUnit = true; // The engineering unit after the bar scale's last number.
    // How HOLD takes its reading (Measurement > Hold): Manual by default and in files before it. Only
    // the mode and its parameters are kept: a cycle or a snapshot never outlives a restart.
    dmm::HoldSettings hold;
    dmm::TemperatureSettings temperature;
    bool analog() const { return display == AnalogDisplay; } // Which of the two orders shows.
    bool hybrid() const { return preset == HybridPreset; }
    // The look the DMM shows: Hybrid's constant, or the Custom look.
    unsigned shownPalette() const { return hybrid() ? MeterAppearanceCatalog::HybridPalette : palette; }
    unsigned shownFace() const { return hybrid() ? MeterAppearanceCatalog::HybridFace : face; }
    const MeterAppearance &shownAppearance() const { return hybrid() ? MeterAppearanceCatalog::hybrid() : appearance; }
    // Before a manual edit: Hybrid becomes the Custom look it shows (the previous Custom is replaced),
    // and the preset Custom. Custom: nothing changes.
    void custom() {
        if (!hybrid()) return;
        palette = MeterAppearanceCatalog::HybridPalette; face = MeterAppearanceCatalog::HybridFace;
        appearance = MeterAppearanceCatalog::hybrid(); preset = CustomPreset;
    }
    bool valid() const {
        return display < Displays && statistics <= 1 && bar <= 1 && automatic <= 1 && range < 8 && face < Faces && palette < Palettes && preset < Presets &&
               rangeFunction <= MeterFunctionCount && appearance.valid() && digitalArrangement.valid() && analogArrangement.valid() &&
               hold.valid() && temperature.valid() && log.valid();
    }
};
} // namespace openosc
