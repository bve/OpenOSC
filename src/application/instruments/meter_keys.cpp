#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/meter_properties.hpp"

namespace openosc {
void ApplicationController::meterKeys(uint32_t keys, uint32_t now) {
    // AUTO/RANGE is meterRangeKeys' (its own events, not the generic short press). A function without
    // ranges (diode, continuity, no reading yet; DataAccess::dmmRanged): the range keys are ignored,
    // nothing sent, nothing said.
    const bool ranged = data_.dmmRanged();
    if (ranged && (keys & (Keypad::Up | Keypad::Down)) && data_.meter().latest().range == dmm::RangeMode::Manual &&
        !meterCommand(data_.stepDmmRange(keys & Keypad::Up ? 1 : -1)))
        view_.notify("Range command busy", now);
    if (keys & (Keypad::Left | Keypad::Right)) {
        constexpr uint32_t n = MeterPreferences::Displays;
        prefs_.meter.display = (prefs_.meter.display + (keys & Keypad::Right ? 1 : n - 1)) % n;
        changed(now); dataRefresh_ = true;
    }
    if (keys & Keypad::Relative) {
        const bool enabled = !(data_.meter().relative().active() || data_.meter().latest().peerRelative);
        if (!data_.relativeMeter(enabled)) view_.notify("REL needs a valid current reading", now);
        dataRefresh_ = true;
    }
    if (keys & Keypad::Menu) {
        meterEditor_ = meterProperties();
        openPanel(Panel::Meter, meterEditor_);
        return;
    }
    if (keys & Keypad::HoldSave) meterHoldKey(now);
    dmm::CommandGroup group = dmm::CommandGroup::Voltage;
    if (keys & Keypad::F2) group = dmm::CommandGroup::ResistanceGroup;
    if (keys & Keypad::F3) group = dmm::CommandGroup::Current;
    if (keys & Keypad::F4)
        group = data_.meter().latest().currentInput ? dmm::CommandGroup::Milliamps : dmm::CommandGroup::Millivolts;
    if ((keys & (Keypad::F1 | Keypad::F2 | Keypad::F3 | Keypad::F4)) && !meterCommand(data_.sendDmmGroup(group)))
        view_.notify("Measurement command busy", now);
}
// AUTO/RANGE qualified press (~50 ms): the factory's 20 20 (703S 08044274, 706S 08044BE4) opens the
// stroke; busy: said once here, its release and hold then do nothing. Not on another screen or panel,
// in Remote or without ranges.
bool ApplicationController::meterRangeContext() const {
    return !remote_ && uiReady_ && panel_ == Panel::None && !state_.settingsOpen && state_.mode == MeasurementMode::Multimeter;
}
void ApplicationController::meterRangePress(uint32_t now) {
    if (!meterRangeContext() || !data_.dmmRanged()) return;
    if (!meterCommand(data_.pressDmmRange())) view_.notify("Range command busy", now);
}
// The physical AUTO/RANGE in the DMM as the factory's KEY5 (KeyGestures' own events, 703S 080459F4 /
// 706S 08046390): the qualified press its 20; the release within 500 ms its 21 (slot0 080442B8 /
// 08044C28); a later release nothing (the factory's long flag); each hold, 2 s and every 2 s held, a
// 22 (slot2 0804423C / 08044BAC). One open stroke at a time; without it (its press refused, a wake took
// the key) release and hold do nothing.
void ApplicationController::meterRangeKeys(const KeyEvents &events, uint32_t now) {
    constexpr uint32_t Key = Keypad::AutoRange;
    serviceMeterRange();
    if (events.press & Key) meterRangePress(now);
    if (!data_.dmmRangeOpen()) return;
    if (events.holdRepeat & Key) data_.holdDmmRange();
    if (events.release & Key) data_.releaseDmmRange(true);
    else if (events.releaseLate & Key) data_.releaseDmmRange(false);
}
// When the DMM screen no longer has AUTO/RANGE (a panel or the settings opened, Remote, another
// screen, no UI) its open stroke is closed at once: within the factory's window its 21, later nothing
// (DataAccess::closeDmmRange). Every loop pass (serviceInstruments, before the key events) and before
// each KEY5 event, so no release or hold has to come first.
void ApplicationController::serviceMeterRange() {
    if (data_.dmmRangeOpen() && !meterRangeContext()) data_.closeDmmRange();
}
// The DMM panel as the instrument and the preferences have it now.
ui::MeterProperties ApplicationController::meterProperties() const {
    const auto &meter = data_.meter();
    return ui::MeterProperties(meter.displayed(),prefs_.meter.display,prefs_.meter.statistics,
        meter.relative().active() || meter.latest().peerRelative,
        data_.meterLogger().active(),data_.meterSerial().enabled(),data_.generator().enabled(),
        prefs_.meter.face, prefs_.meter.palette, &themes_.theme(), prefs_.meter.hold.mode).barGraph(prefs_.meter.bar);
}
// The open DMM panel after the instrument changed on its own (output stopped, logging ended, new function).
void ApplicationController::refreshMeterPanel() {
    if (panel_ != Panel::Meter) return;
    meterEditor_ = meterProperties();
    view_.properties(meterEditor_, navigator_);
}
void ApplicationController::meterPanelKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = navigator_.keys(keys, meterEditor_);
    if (outcome == Outcome::Back) { closePanel(); return; }
    if (outcome == Outcome::Open && navigator_.row() == ui::MeterProperties::Layout) { openMeterLayout(); return; }
    if (outcome == Outcome::Open && navigator_.row() == ui::MeterProperties::Hold) {
        holdEditor_ = ui::MeterHoldProperties(prefs_.meter.hold);
        openPanel(Panel::MeterHold, holdEditor_);
        return;
    }
    if (outcome == Outcome::Open) { // Appearance: its own list, over the base the panel shows.
        // The theme in force and the DMM cards' colour, as MeterScreen styles with: Default/Base show the real base.
        const auto &theme = themes_.theme();
        const uint32_t cards = theme.elements[unsigned(MeasurementMode::Multimeter)][unsigned(ElementId::Parameters)].color;
        appearanceEditor_ = ui::MeterAppearanceProperties(prefs_.meter.palette, prefs_.meter.face, prefs_.meter.appearance,
                                                          appearanceEditor_.element(), &theme, cards);
        openPanel(Panel::MeterAppearance, appearanceEditor_);
        return;
    }
    if (outcome == Outcome::Activate) data_.meter().resetStatistics(); // The one action row.
    if (outcome == Outcome::Changed) meterChanged(navigator_.row(), now);
    view_.properties(meterEditor_, navigator_);
}
// Appearance: every committed choice and reset is taken at once and saved; the reading, range,
// REL, HOLD and statistics are untouched (only MeterScreen's style changes). Back: the DMM panel.
void ApplicationController::meterAppearanceKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    using Editor = ui::MeterAppearanceProperties;
    const auto outcome = navigator_.keys(keys, appearanceEditor_);
    if (outcome == Outcome::Back) {
        meterEditor_ = meterProperties();
        openPanel(Panel::Meter, meterEditor_, ui::MeterProperties::Appearance);
        return;
    }
    if (outcome == Outcome::Activate) { // A reset may drop the R/G/B rows: the action keeps its selection at its new index.
        const auto action = appearanceEditor_.at(navigator_.row());
        appearanceEditor_.reset(action);
        navigator_.reset(appearanceEditor_.index(action));
    }
    if (outcome == Outcome::Changed || outcome == Outcome::Activate) {
        auto &meter = prefs_.meter;
        const bool element = appearanceEditor_.at(navigator_.row()) == Editor::Element; // Selects, sets nothing.
        if (!element) {
            meter.palette = appearanceEditor_.palette(); meter.face = appearanceEditor_.face(); meter.appearance = appearanceEditor_.appearance();
            changed(now); dataRefresh_ = true;
        }
    }
    view_.properties(appearanceEditor_, navigator_);
}
// A choice in the DMM panel, taken at once: only the row that was changed. A look (Display, colours,
// digits, Statistics) sets its preference and sends nothing: the panel's snapshot of Range, REL, log
// and serial (from when it opened, or a HOLD's reading) is never compared with the instrument then.
// Range, REL, log and serial apply that one choice; what is refused is said and the panel shows the
// instrument as it is.
void ApplicationController::meterChanged(unsigned row, uint32_t now) {
    using Row = ui::MeterProperties::Row;
    const auto &meter=data_.meter();
    const auto refused = [&](const char *why) { view_.notify(why, now); meterEditor_ = meterProperties(); };
    switch (row) {
    case Row::Range: // Already so: nothing (not even the hold mode's window restarts).
        if (meterEditor_.automatic() != (meter.latest().range == dmm::RangeMode::Auto) &&
            !meterCommand(data_.setDmmAuto(meterEditor_.automatic())))
            return refused("Range command busy");
        return;
    case Row::Relative: // A REL command restarts the statistics: only when it changes REL.
        if (meterEditor_.relative() != (meter.relative().active() || meter.latest().peerRelative) &&
            !data_.relativeMeter(meterEditor_.relative()))
            return refused("REL needs a valid current reading");
        return;
    case Row::Logging: {
        auto &logger=data_.meterLogger();
        if (meterEditor_.logging() == logger.active()) return;
        if (logger.active()) logger.finish();
        else if (!logger.begin(1024*1024)) return refused("Cannot start CSV logging");
        return;
    }
    case Row::SerialOutput:
        if (meterEditor_.serial() == data_.meterSerial().enabled()) return;
        if (!data_.serialMeter(meterEditor_.serial())) return refused("Serial output busy; turn GEN off");
        serialWatched_=data_.meterSerial().enabled();
        return;
    case Row::Display: prefs_.meter.display=meterEditor_.display(); break;
    case Row::Statistics: prefs_.meter.statistics=meterEditor_.statistics(); break;
    case Row::BarGraph: prefs_.meter.bar=meterEditor_.barGraph(); break;
    case Row::Digits: prefs_.meter.face=meterEditor_.face(); break;
    case Row::Colors: prefs_.meter.palette=meterEditor_.palette(); break;
    default: return;
    }
    changed(now); dataRefresh_=true;
}
} // namespace openosc
