#include "openosc/application/controller.hpp"
#include "openosc/data/fat_file.hpp"
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
            if (uiReady_) { view_.notify(Tooltip::Critical, generator::faultText(generator.fault), now); dataRefresh_ = true; }
        }
    }
    const bool visible = uiReady_;
    if (data_.dmmRestarts() != dmmRestarts_) { // The chip restarted in its default function.
        dmmRestarts_ = data_.dmmRestarts();
        if (data_.measurementMode() == MeasurementMode::Multimeter) {
            meterRestore_.arm(data_.selectDmmFunction(dmm::Function(prefs_.meterFunction)));
            if (visible) view_.notify(Tooltip::Critical, "DMM stopped responding: restarted", now);
        }
    }
    if (data_.dmmReselects() != dmmReselects_) {
        dmmReselects_ = data_.dmmReselects();
        if (data_.measurementMode() == MeasurementMode::Multimeter)
            meterRestore_.arm(data_.selectDmmFunction(dmm::Function(prefs_.meterFunction)));
    }
    const auto selection = data_.dmmCommandStatus();
    if (serialWatched_ && !data_.meterSerial().enabled()) {
        if (visible && data_.meterSerial().status()!=DataStatus::Ok) view_.notify(Tooltip::Critical, "Serial output failed",now);
        serialWatched_=false;
        if (visible) refreshMeterPanel();
    }
    if (selection != lastSelection_) {
        if (visible && (selection == DataStatus::Timeout || selection == DataStatus::HardwareError)) {
            char text[64];
            std::snprintf(text, sizeof text, "Multimeter %s command was not confirmed", data_.dmmCommandName());
            view_.notify(Tooltip::Critical, text, now);
        }
        // One RANGE press answered by packets without MANU (KeyCommand, RangeSelector): no second press.
        if (visible && selection == DataStatus::Unsupported) view_.notify(Tooltip::Advisory, "Meter did not report a manual range", now);
        lastSelection_ = selection;
        if (visible) refreshMeterPanel();
    }
    // Decided once, or dropped when it can no longer happen (not the DMM, a failed selection).
    const bool restoreDue = meterRestore_.due(data_.measurementMode(), data_.dmmSelectionStatus(), data_.dmmBusy());
    if (data_.measurementMode() == MeasurementMode::Multimeter && data_.dmmSelectionStatus() == DataStatus::Ok && !data_.dmmBusy()) {
        const auto &result = data_.meter().latest();
        const auto &meter = data_.meter();
        const auto function = meter.thermocouple() ? dmm::Function::Temperature : dmm::Functions::identify(result);
        if (restoreDue) { // Another or an unknown function: restores() is false, nothing is sent, prefs stay.
            if (MeterRangeMemory::restores(prefs_.meter, prefs_.meterFunction, function, result.range)) {
                const bool accepted = prefs_.meter.automatic ? data_.setDmmAuto(true) : data_.setDmmRange(prefs_.meter.range);
                if (!accepted && visible) view_.notify(Tooltip::Critical, "Saved DMM range could not be applied",now);
                if (accepted) return;
            }
        }
        // A native temperature packet can be displayed, but cannot be selected at the next boot.
        if (function != dmm::Function::Unknown && (function != dmm::Function::Temperature || meter.thermocouple()) &&
            prefs_.meterFunction != unsigned(function)) {
            prefs_.meterFunction = state_.meter.function = unsigned(function); changed(now); dataRefresh_ = true;
            if (visible) refreshMeterPanel();
        }
        // The confirmed range into prefs; not after a failed command (the saved choice stays).
        if (data_.dmmCommandStatus() == DataStatus::Ok && MeterRangeMemory::mirror(prefs_.meter, function, result)) changed(now);
    }
    if (recordWatched_ && !data_.scopeRecording()) {
        const auto status = data_.scopeRecordStatus();
        if (visible) {
            char text[96];
            recordResult(status, text, sizeof text);
            view_.notify(status == DataStatus::Ok ? Tooltip::Done : Tooltip::Critical, text, now);
        }
        recordWatched_ = false;
    }
    auto &logger = data_.meterLogger();
    if (logger.active()) loggerWatched_ = true;
    else if (loggerWatched_) {
        if (visible) view_.notify(logger.status() == DataStatus::Ok ? Tooltip::Done : Tooltip::Critical, logger.status() == DataStatus::Ok ? logger.path() : "CSV logging failed", now);
        loggerWatched_ = false;
        if (visible) refreshMeterPanel();
    }
}
// Analyze -> Save RAW / Save CSV, the waveform (never the decoder's CSV): the file, or why there is none.
// No space: what the record needs (ScopeRecorder::required: RAW exact, CSV counted row by row) with the
// volume's headroom, and what is free, in KiB rounded up and down so they never look equal while one does not fit.
void ApplicationController::recordResult(DataStatus status, char *text, size_t capacity) {
    const auto &recorder = data_.scopeRecorder();
    const bool csv = recorder.format() == ScopeRecorder::Format::Csv;
    if (status == DataStatus::Ok) { std::snprintf(text, capacity, "%s", recorder.path()); return; }
    if (status == DataStatus::NoSpace && recorder.required()) {
        const auto needed = static_cast<unsigned long>((recorder.required() + FatDataFile::FreeHeadroom + 1023) / 1024);
        const auto free = static_cast<unsigned long>(fs_.freeBytes() / 1024);
        std::snprintf(text, capacity, "Waveform %s needs %lu KiB, %lu KiB free%s",
                      csv ? "CSV" : "RAW", needed, free, csv ? "\nTry RAW or a shorter record." : "");
        return;
    }
    std::snprintf(text, capacity, "%s", status == DataStatus::NoSpace ? "Not enough free space for this waveform"
                                       : status == DataStatus::IoError ? "Waveform save failed: storage error"
                                       : "Waveform save failed / no completed capture");
}
} // namespace openosc
