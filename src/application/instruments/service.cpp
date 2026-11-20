#include "openosc/application/controller.hpp"
#include <cstdio>
#include "openosc/application/meter_range_memory.hpp"

namespace openosc {
void ApplicationController::serviceInstruments(uint32_t now) {
    serviceMeterRange();        // A panel, the settings or Remote took AUTO/RANGE: its stroke closed now.
    followGeneratorOutput(now); // Startup output: a user's ON/OFF once confirmed.
    { // A stop the generator made by itself (DAC underrun, above the clock's DAC limit): one toast each.
        const auto generator = data_.generator().snapshot();
        if (generator.faults != generatorFaults_) {
            generatorFaults_ = generator.faults;
            if (uiReady_) { view_.notify(generator::faultText(generator.fault), now); dataRefresh_ = true; }
        }
    }
    const bool visible = uiReady_;
    if (data_.dmmRestarts() != dmmRestarts_) { // The chip restarted in its default function.
        dmmRestarts_ = data_.dmmRestarts();
        if (data_.measurementMode() == MeasurementMode::Multimeter) {
            meterRestore_.arm(data_.selectDmmFunction(dmm::Function(prefs_.meterFunction)));
            if (visible) view_.notify("DMM stopped responding: restarted", now);
        }
    }
    if (data_.dmmReselects() != dmmReselects_) {
        dmmReselects_ = data_.dmmReselects();
        if (data_.measurementMode() == MeasurementMode::Multimeter)
            meterRestore_.arm(data_.selectDmmFunction(dmm::Function(prefs_.meterFunction)));
    }
    const auto selection = data_.dmmCommandStatus();
    if (serialWatched_ && !data_.meterSerial().enabled()) {
        if (visible && data_.meterSerial().status()!=DataStatus::Ok) view_.notify("Serial output failed",now);
        serialWatched_=false;
        if (visible) refreshMeterPanel();
    }
    if (selection != lastSelection_) {
        if (visible && (selection == DataStatus::Timeout || selection == DataStatus::HardwareError)) {
            char text[64];
            std::snprintf(text, sizeof text, "Multimeter %s command was not confirmed", data_.dmmCommandName());
            view_.notify(text, now);
        }
        // One RANGE press answered by packets without MANU (KeyCommand, RangeSelector): no second press.
        if (visible && selection == DataStatus::Unsupported) view_.notify("Meter did not report a manual range", now);
        lastSelection_ = selection;
        if (visible) refreshMeterPanel();
    }
    // Decided once, or dropped when it can no longer happen (not the DMM, a failed selection).
    const bool restoreDue = meterRestore_.due(data_.measurementMode(), data_.dmmSelectionStatus(), data_.dmmBusy());
    if (data_.measurementMode() == MeasurementMode::Multimeter && data_.dmmSelectionStatus() == DataStatus::Ok && !data_.dmmBusy()) {
        const auto &result = data_.meter().latest();
        const auto &meter = data_.meter();
        const auto function = meter.typeK() ? dmm::Function::Temperature : dmm::Functions::identify(result);
        if (restoreDue) { // Another or an unknown function: restores() is false, nothing is sent, prefs stay.
            if (MeterRangeMemory::restores(prefs_.meter, prefs_.meterFunction, function, result.range)) {
                const bool accepted = prefs_.meter.automatic ? data_.setDmmAuto(true) : data_.setDmmRange(prefs_.meter.range);
                if (!accepted && visible) view_.notify("Saved DMM range could not be applied",now);
                if (accepted) return;
            }
        }
        // A native temperature packet can be displayed, but cannot be selected at the next boot.
        if (function != dmm::Function::Unknown && (function != dmm::Function::Temperature || meter.typeK()) &&
            prefs_.meterFunction != unsigned(function)) {
            prefs_.meterFunction = state_.meter.function = unsigned(function); changed(now); dataRefresh_ = true;
            if (visible) refreshMeterPanel();
        }
        // The confirmed range into prefs; not after a failed command (the saved choice stays).
        if (data_.dmmCommandStatus() == DataStatus::Ok && MeterRangeMemory::mirror(prefs_.meter, function, result)) changed(now);
    }
    if (recordWatched_ && !data_.scopeRecording()) {
        const auto status = data_.scopeRecordStatus();
        if (visible) view_.notify(status == DataStatus::Ok ? data_.scopeRecorder().path() :
                     status == DataStatus::NoSpace ? "Not enough free space for this record" :
                     "Waveform save failed / no completed capture", now);
        recordWatched_ = false;
    }
    auto &logger = data_.meterLogger();
    if (logger.active()) loggerWatched_ = true;
    else if (loggerWatched_) {
        if (visible) view_.notify(logger.status() == DataStatus::Ok ? logger.path() : "CSV logging failed", now);
        loggerWatched_ = false;
        if (visible) refreshMeterPanel();
    }
}
} // namespace openosc
