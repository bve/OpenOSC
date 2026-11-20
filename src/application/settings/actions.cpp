#include "openosc/application/controller.hpp"
#include <cstdio>

namespace openosc {
void ApplicationController::settingsAction(ui::SettingsAction action, uint32_t now) {
    switch (action) {
    case ui::SettingsAction::CalibrateHardwareZero:
        if (data_.measurementMode()!=MeasurementMode::Oscilloscope) { view_.notify("Select Scope first",now); break; }
        confirmOperation(ui::Confirmation("GROUND BOTH SCOPE INPUTS?","Stop GEN / set offset PWM","Any DC on an input is taken as offset"),&ApplicationController::beginHardwareZero);
        break;
    case ui::SettingsAction::CalibrateZero:
        if (data_.measurementMode()!=MeasurementMode::Oscilloscope) { view_.notify("Select Scope first",now); break; }
        confirmOperation(ui::Confirmation("DISCONNECT SCOPE INPUTS?","Stop GEN / calibrate","Checks CH1/CH2 across all ranges"),&ApplicationController::beginSoftwareZero);
        break;
    case ui::SettingsAction::ReloadZero:
        view_.notify(data_.reloadCalibration() ? data_.zeroFile().repaired() || data_.pwmFile().repaired() || data_.gainFile().repaired() ||
                     data_.interleaveFile().repaired() ? "Calibration JSON repaired" : "Calibration loaded for new captures" :
            "Cannot load calibration JSON",now);
        break;
    case ui::SettingsAction::CalibrateGain: confirmGain(now); break;
    case ui::SettingsAction::FactoryGain:
        confirmOperation(ui::Confirmation("USE FACTORY GAIN?","Disable user gain","Saved gain values are kept"),&ApplicationController::disableGain);
        break;
    case ui::SettingsAction::SaveInterleave:
    case ui::SettingsAction::CancelInterleave: case ui::SettingsAction::ResetInterleave:
        interleaveAction(action,now);
        break;
    case ui::SettingsAction::FactoryZero:
        confirmOperation(ui::Confirmation("USE FACTORY ZERO?","Disable hardware + software","Saved zero values are kept"),&ApplicationController::disableZero);
        break;
    case ui::SettingsAction::ReloadPreferences: {
        const bool discarded = saves_.settingsPending() || saves_.themePending();
        if (!preferences_.load()) {
            view_.notify(preferences_.error(), now);
            break;
        }
        saves_.dropTheme();
        prefs_ = preferences_.values();
        saves_.dropSettings();
        applyPreferences();
        const auto applied = applyDataPreferences();
        if (prefs_.useBuiltinTheme) themes_.defaults();
        else themes_.reload();
        buildView();
        char result[112]; applied.text(result, sizeof result);
        char text[160];
        std::snprintf(text, sizeof text, "%s%s", discarded ? "Unsaved changes discarded. " : "", result);
        view_.notify(text, now);
        break;
    }
    case ui::SettingsAction::ReloadTheme:
        if (themes_.reload()) {
            const bool discarded = saves_.themePending();
            saves_.dropTheme();
            prefs_.useBuiltinTheme = 0;
            changed(now);
            buildView();
            const char *notice = ThemeNotice::text(themes_);
            view_.notify(notice ? notice : discarded ? "Theme loaded; plot changes discarded" : "Theme loaded", now);
        } else view_.notify(themes_.error(), now);
        break;
    case ui::SettingsAction::BuiltinTheme:
        saves_.dropTheme();
        themes_.defaults();
        prefs_.useBuiltinTheme = 1;
        changed(now);
        buildView();
        break;
    case ui::SettingsAction::Storage:
        fs_.refreshFree();
        settingsInfo_ = {nor_.geometry().jedecId, nor_.geometry().capacity, fs_.freeBytes(), fs_.mounted()};
        break;
    case ui::SettingsAction::Recovery: recoveryFromSettings(now); break;
    case ui::SettingsAction::Restart: confirmShutdown(ShutdownAction::Restart); break;
    case ui::SettingsAction::RestoreSettings:
        confirmOperation(ui::Confirmation("RESTORE SETTINGS?","Restore and restart"),&ApplicationController::restoreSettings);
        break;
    case ui::SettingsAction::Images: openImages(now); break;
    case ui::SettingsAction::ClearImages:
        if (shots_.busy()) { view_.notify("Finish screenshot first",now); break; }
        confirmOperation(ui::Confirmation("CLEAR SCREENSHOTS?","Delete all BMP images"),&ApplicationController::clearImages);
        break;
    case ui::SettingsAction::UsbRemote:
        if (!prefs_.usbController) view_.notify("Enable USB controller first", now); // No link to be remote over.
        else if (!setRemote(true)) view_.notify("Finish active operations first",now);
        else view_.notify("USB Remote: hold MENU to exit",now);
        break;
    case ui::SettingsAction::CalibrateInterleave: interleaveOffsetAction(now); break; // ZT703S.
#if OPENOSC_BOARD == 703
    case ui::SettingsAction::CalibrateInterleaveMatch: openInterleaveMatch(now); break;
#endif
    case ui::SettingsAction::Close: closeSettings(); break;
    default: break;
    }
}
} // namespace openosc
