#include "openosc/application/controller.hpp"
#include "openosc/ui/editors/zero_progress.hpp"
#include "openosc/ui/editors/gain_progress.hpp"
#include "openosc/platform/clocks.hpp"
#include "openosc/platform/input.hpp"
#include <cmath>
#include <cstdio>

namespace openosc {
void ApplicationController::beginZero(uint32_t now,bool hardware) {
    confirmationHandler_=nullptr;
    if (!data_.calibrateZero(now,hardware)) {
        using Result=calibration::ZeroOperation::Result;
        const auto result=data_.zeroOperation().result();
        view_.notify(result==Result::Uncalibrated || result==Result::IoError || result==Result::HardwareError ? Tooltip::Critical : Tooltip::Unavailable,
            result==Result::Uncalibrated ? "Factory calibration unavailable" :
            result==Result::IoError ? "Calibration disk unavailable" :
            result==Result::Shadowed ? "Hardware zero in force: use factory zero first" : result==Result::HardwareError ?
            "Cannot stop generator" : "Zero calibration unavailable / busy",now);
        panel_=Panel::None; dataRefresh_=true; view_.measurement(state_); return;
    }
    state_.settingsOpen=false; panel_=Panel::Zero; zeroWatched_=true; zeroProgress_=UINT32_MAX;
    dataRefresh_=true;
    serviceZeroUi(now);
}
void ApplicationController::disableZero(uint32_t now) {
    confirmationHandler_=nullptr; panel_=Panel::None;
    using Result=calibration::ZeroSelection::Result;
    const auto result=data_.factoryZero();
    view_.measurement(state_); dataRefresh_=true;
    view_.notify(result==Result::Factory || result==Result::HardwareKept ? Tooltip::Done : result==Result::Unchanged ? Tooltip::Critical : Tooltip::Unavailable,
        result==Result::Factory ? "Factory zero applies to new captures" :
        result==Result::Unread ? "Zero JSON unread: Reload zero JSON first" :
        result==Result::Unchanged ? "Cannot save zero selection; unchanged" :
        result==Result::HardwareKept ? "Software zero off; hardware zero kept" : "Zero selection unavailable / busy",now);
}
void ApplicationController::zeroKeys(uint32_t keys,uint32_t now) {
    using Phase=calibration::ZeroOperation::Phase;
    const auto phase=data_.zeroOperation().phase();
    if (data_.gainRunning() && (phase==Phase::Applying || phase==Phase::Collecting)) { gainKeys(keys,now); return; }
    if (keys&(Keypad::F4|Keypad::Mode|Keypad::Menu)) data_.cancelZero(now);
}
// The gain's own rows: Up/Down choose, MENU acts (Reference applied, Save, Cancel); F4, Left and
// MODE cancel. Only Save keeps a result.
void ApplicationController::gainKeys(uint32_t keys,uint32_t now) {
    if (!keys) return;
    using Outcome=ui::PropertyNavigator::Outcome;
    using Action=ui::GainProgress::Action;
    ui::GainProgress rows(data_.gainCalibration());
    if (keys&Keypad::Mode) { data_.cancelZero(now); return; }
    const auto outcome=navigator_.keys(keys,rows);
    if (outcome==Outcome::Back) data_.cancelZero(now);
    else if (outcome==Outcome::Activate) {
        const auto action=rows.action(navigator_.row());
        if (action==Action::Cancel) data_.cancelZero(now);
        else if (action==Action::Proceed) data_.proceedGain(now);
        else if (action==Action::Save) data_.acceptGain();
    }
    serviceZeroUi(now); // Redraws for a new row or step (both are part of what it shows).
}
void ApplicationController::confirmGain(uint32_t now) {
    if (data_.measurementMode()!=MeasurementMode::Oscilloscope) { view_.notify(Tooltip::Unavailable, "Select Scope first",now); return; }
    const auto &d=calibrationDraft_;
    double span=0;
    if (!data_.gainSpan(d.channel,d.range,d.referenceMillivolts/1000.0,span) || !calibration::GainCalibration::spanAllowed(span)) {
        view_.notify(Tooltip::Unavailable, "Expected span must be 40..110 codes",now); return;
    }
    std::snprintf(gainQuestion_,sizeof gainQuestion_,"CONNECT CH%u TIP TO GND",d.channel+1);
    confirmOperation(ui::Confirmation(gainQuestion_,"Stop GEN / measure","Disconnect the tip from the signal"),&ApplicationController::beginGain);
}
void ApplicationController::beginGain(uint32_t now) {
    confirmationHandler_=nullptr;
    const auto &d=calibrationDraft_;
    const auto status=data_.calibrateGain(now,d.channel,d.range,d.referenceMillivolts/1000.0);
    if (status!=DataStatus::Ok) {
        using Result=calibration::ZeroOperation::Result;
        const auto result=data_.zeroOperation().result();
        view_.notify(status==DataStatus::InvalidArgument ? Tooltip::Unavailable : status==DataStatus::Uncalibrated || result==Result::Uncalibrated ||
            result==Result::IoError || result==Result::HardwareError ? Tooltip::Critical : Tooltip::Unavailable,
            status==DataStatus::InvalidArgument ? "Expected span must be 40..110 codes" :
            status==DataStatus::Uncalibrated || result==Result::Uncalibrated ? "Factory calibration unavailable" :
            result==Result::IoError ? "Gain JSON unread or disk unavailable" : result==Result::HardwareError ? "Cannot stop generator" :
            "Gain calibration unavailable / busy",now);
        panel_=Panel::None; dataRefresh_=true; view_.measurement(state_); return;
    }
    state_.settingsOpen=false; panel_=Panel::Zero; zeroWatched_=true; zeroProgress_=UINT32_MAX;
    dataRefresh_=true;
    serviceZeroUi(now);
}
void ApplicationController::disableGain(uint32_t now) {
    confirmationHandler_=nullptr; panel_=Panel::None;
    const bool success=data_.factoryGain();
    view_.measurement(state_); dataRefresh_=true;
    view_.notify(success ? Tooltip::Done : Tooltip::Critical, success ? "Factory gain applies to new captures" : "Gain JSON unread or not saved; unchanged",now);
}
// InterleaveOffset: its finished edit, in force for new records (a preview, not saved); Save,
// Cancel (back to the saved one) and Reset to factory are the profile's own actions. Afterwards
// the Offset row shows what is in force, also after a refusal.
void ApplicationController::interleaveAction(ui::SettingsAction action,uint32_t now) {
    using A=ui::SettingsAction;
    const auto status=action==A::InterleaveOffset ? data_.applyInterleave(calibrationDraft_.interleaveTenths/10.0f) :
        action==A::SaveInterleave ? data_.saveInterleave() : action==A::CancelInterleave ? data_.cancelInterleave() : data_.resetInterleave();
    calibrationDraft_.interleaveTenths=int32_t(std::lround(data_.interleaveOffset()*10));
    view_.notify(status==DataStatus::Ok ? Tooltip::Done : status==DataStatus::Unsupported || status==DataStatus::Busy ||
                 status==DataStatus::InvalidArgument ? Tooltip::Unavailable : Tooltip::Critical,
                 status==DataStatus::Ok ? (action==A::InterleaveOffset ? "Offset in force for new records (not saved)" :
                                           action==A::SaveInterleave ? "Interleave offset saved" :
                                           action==A::CancelInterleave ? "Saved interleave offset in force" : "Factory interleave offset saved") :
                 status==DataStatus::Unsupported ? "No interleaved capture on this model" :
                 status==DataStatus::Busy ? "Finish the calibration first" :
                 status==DataStatus::InvalidArgument ? "Offset outside +-16 codes" : "Interleave JSON not saved; unchanged",now);
}
void ApplicationController::serviceZeroUi(uint32_t now) {
    if (!zeroWatched_) return;
    if (shutdown_!=Shutdown::None) { zeroWatched_=false; return; }
    const auto &operation=data_.zeroOperation();
    if (operation.busy()) {
        const uint32_t progress=(operation.calibration().percent()<<8)|unsigned(operation.phase())|
            (uint32_t(operation.calibration().state())<<16);
        using Phase=calibration::ZeroOperation::Phase;
        if (data_.gainRunning() && (operation.phase()==Phase::Applying || operation.phase()==Phase::Collecting)) {
            const ui::GainProgress rows(data_.gainCalibration());
            const uint32_t step=uint32_t(data_.gainCalibration().step());
            if (step!=(zeroProgress_>>24 & 0xf)) navigator_.reset(rows.defaultRow()); // A new step: its main action.
            const uint32_t shown=progress|step<<24|uint32_t(navigator_.row())<<28;
            if (panel_==Panel::Zero && shown!=zeroProgress_) { zeroProgress_=shown; view_.properties(rows,navigator_.row()); }
            return;
        }
        if (panel_==Panel::Zero && progress!=zeroProgress_) {
            zeroProgress_=progress;
            view_.properties(ui::ZeroProgress(operation.calibration(),operation.phase()),3);
        }
        return;
    }
    zeroWatched_=false;
    if (panel_!=Panel::Zero) return;
    panel_=Panel::None; dataRefresh_=true; view_.measurement(state_);
    using Result=calibration::ZeroOperation::Result;
    const auto result=operation.result();
    const char *reason=operation.calibration().reason();
    const char *outcome=result==Result::Complete ? "saved" : result==Result::Cancelled ? "cancelled" :
        result==Result::InvalidInput ? (reason ? reason : "failed: input not stable / offset too large") : result==Result::IoError ? "save failed; previous kept" :
        result==Result::Uncalibrated ? "failed: factory calibration unavailable" : result==Result::Timeout ? "timed out" :
        result==Result::RestoreFailed ? "done; capture restore failed: RUN or restart" : "failed";
    char message[80];
    std::snprintf(message,sizeof message,"%s %s",operation.calibration().name(),outcome);
    // Saved or cancelled as asked; a run without a usable input (nothing saved); otherwise a failure.
    view_.notify(result==Result::Complete || result==Result::Cancelled ? Tooltip::Done : result==Result::InvalidInput ||
                 result==Result::Timeout ? Tooltip::Advisory : Tooltip::Critical, message, now);
}
} // namespace openosc
