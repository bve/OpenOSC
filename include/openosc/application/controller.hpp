#pragma once
#include "openosc/application/generator_startup.hpp"
#include "openosc/application/meter_restore.hpp"
#include "openosc/application/state.hpp"
#include "openosc/application/preferences_result.hpp"
#include "openosc/input/gestures.hpp"
#include "openosc/instrument/data_access.hpp"
#include "openosc/screenshot/writer.hpp"
#include "openosc/screenshot/catalog.hpp"
#include "openosc/screenshot/reader.hpp"
#include "openosc/settings/preferences_file.hpp"
#include "openosc/settings/theme_store.hpp"
#include "openosc/ui/application_view.hpp"
#include "openosc/ui/lvgl_display.hpp"
#include "openosc/ui/editors/trigger_editor.hpp"
#include "openosc/ui/editors/meter_properties.hpp"
#include "openosc/ui/editors/meter_appearance_properties.hpp"
#include "openosc/ui/editors/meter_hold_properties.hpp"
#include "openosc/ui/editors/arrangement_properties.hpp"
#include "openosc/ui/editors/scope_appearance_properties.hpp"
#include "openosc/ui/editors/scope_editor.hpp"
#include "openosc/ui/editors/decoder_properties.hpp"
#include "openosc/decode/workspace.hpp"
#include "openosc/decode/record_rate.hpp"
#include "openosc/decode/table_text.hpp"
#include "openosc/application/readout_scheduler.hpp"
#include "openosc/application/run_mode_switch.hpp"
#include "openosc/ui/editors/measurement_editor.hpp"
#include "openosc/ui/editors/generator_editor.hpp"
#include "openosc/acquisition/auto_setup.hpp"
#include "openosc/acquisition/trigger_level.hpp"
#include "openosc/ui/scope/cursor_control.hpp"
#include "openosc/data/performance.hpp"
#include "openosc/platform/shutdown.hpp"
#include "openosc/application/shutdown_sequence.hpp"
#include "openosc/application/pending_saves.hpp"
#include "openosc/application/boot_guard.hpp"
#include "openosc/ui/editors/confirmation.hpp"
#include "openosc/ui/editors/property_navigator.hpp"
#include "openosc/ui/scope/display_properties.hpp"
#include "openosc/platform/battery_monitor.hpp"
#include "openosc/ui/scope/counter_text.hpp"

namespace openosc {
class InstrumentBackend;
class InstrumentUsb;
class ApplicationController {
public:
    ApplicationController(LvglDisplay &display, NorFlash &nor, Filesystem &fs, PreferencesFile &preferences,
                          ThemeStore &themes, ScreenshotWriter &shots, DataAccess &data)
        : display_(display), nor_(nor), fs_(fs), preferences_(preferences), themes_(themes), shots_(shots),
          data_(data), images_(fs),imageReader_(fs) {}
    // The 360 MHz guard and what the boot made of it (main.cpp, before run()).
    void bootGuard(BootGuard &guard, const BootGuard::Start &start) { guard_ = &guard; bootStart_ = start; }
    [[noreturn]] void run();
    enum class ModeChange : uint8_t { Selected, Busy, Refused };

private:
    friend class InstrumentBackend;
    friend class ScopeControllerProbe; // Native tests drive the key handlers directly.
    bool setRemote(bool enabled);
    ModeChange selectMode(MeasurementMode next, uint32_t now); // MODE key and SYST:MODE.
    InstrumentUsb *usb_ = nullptr;
    // remote_: USB Remote control (keys locked except MENU/REL hold exits, no idle power-off);
    // the display keeps running. uiReady_: the LVGL UI exists — every view update, render and
    // notification depends on it; false only for the headless fallback of a failed display init.
    bool remote_ = false, uiReady_ = false;
    plot::CounterText counterText_;
    bool sectionDirect_ = false; // A Display section opened from Analyze (FFT): Back returns there.
    static constexpr unsigned PanDoubling = 8;
    unsigned panRepeats_ = 0;
    PerformanceCounter performance_;
    void onKeys(const KeyEvents &events, uint32_t now);
    void settingsKeys(uint32_t keys, uint32_t now);
    void showSettings();
    void closeSettings();
    void settingsAction(ui::SettingsAction action, uint32_t now);
    // The run mode (core clock) changed in place (RunModeSwitch); the menu, SCPI and Remote share it.
    // With `persist` the setting is stored when the target runs; Remote's changes are transient.
    RunModeSwitch::Result changeRunMode(uint32_t mode, uint32_t now, bool persist = true);
    void recoveryFromSettings(uint32_t now);
    void serviceRecovery(uint32_t now);
    bool recoveryPending_=false;
    uint32_t recoveryAt_=0;
    void confirmShutdown(ShutdownAction action);
    using ConfirmHandler=void (ApplicationController::*)(uint32_t);
    void confirmOperation(const ui::Confirmation &question,ConfirmHandler handler);
    void leaveConfirmation(uint32_t now); // Back to where the confirmation was asked from.
    void restoreSettings(uint32_t now);
    void openImages(uint32_t now);
    void showImages();
    void loadImage(uint32_t now);
    void imageKeys(uint32_t keys,uint32_t now);
    void serviceImages(uint32_t now);
    void deleteImage(uint32_t now);
    void clearImages(uint32_t now);
    void closeImages();
    void beginZero(uint32_t now,bool hardware);
    void beginSoftwareZero(uint32_t now) { beginZero(now,false); }
    void beginHardwareZero(uint32_t now) { beginZero(now,true); }
    void disableZero(uint32_t now);
    void zeroKeys(uint32_t keys,uint32_t now);
    // Gain (Settings / Scope calibration / Gain) and the ZT706S interleave offset.
    void confirmGain(uint32_t now);
    void beginGain(uint32_t now);
    void disableGain(uint32_t now);
    void gainKeys(uint32_t keys,uint32_t now);
    void interleaveAction(ui::SettingsAction action,uint32_t now);
    ui::SettingsProperties settingsRows() const; // With the calibration pages' draft and status.
    ui::CalibrationDraft calibrationDraft_;
    char gainQuestion_[2][40]{}; // The gain confirmation's texts (Confirmation keeps pointers).
    void serviceZeroUi(uint32_t now);
    // Scope calibration > Interleave offset (ZT703S): its start after the confirmation, its result.
    void interleaveOffsetAction(uint32_t now); // MENU on its row: confirm, or cancel a running one.
    void beginInterleaveOffset(uint32_t now);
    // Interleave CH1 (ZT703S) from the menu or SCPI: applied at once and saved; false when refused.
    bool selectInterleave(bool on, uint32_t now);
    void serviceInterleaveOffsetUi(uint32_t now);
    // The loop pass's overlay pause of full-rate capture (DataAccess::pauseCapture).
    void pauseHiddenCapture(uint32_t now, bool enumerating);
    bool interleaveWatched_=false;
    bool zeroWatched_=false;
    uint32_t zeroProgress_=UINT32_MAX;
    ConfirmHandler confirmationHandler_=nullptr;
    bool confirmationImages_=false;
    bool confirmationZero_=false;
    bool confirmationSettings_=false;
    ui::Confirmation confirmation_{ShutdownAction::PowerOff};
    void confirmationKeys(uint32_t keys, uint32_t now);
    void requestShutdown(uint32_t now);
    void serviceShutdown(uint32_t now);
    bool servicePower(uint32_t keys,uint32_t now);
    IdlePolicy idle_;
    BatteryMonitor battery_;
    power::LowBatteryPolicy lowBattery_;
    bool dimmed_=false;
    using Shutdown = ShutdownAction;
    // The running shutdown (docs/POWER.md). shutdown_ mirrors shutdownSequence_.action() for the
    // places that only ask whether one runs.
    class ShutdownSteps;
    ShutdownSequence shutdownSequence_;
    Shutdown shutdown_ = Shutdown::None;
    ShutdownAction confirmAction_ = ShutdownAction::PowerOff;
    void changed(uint32_t now);
    void store(uint32_t now);
    void applyPreferences();
    PreferencesResult applyDataPreferences();
    // Boot only, after applyDataPreferences: Startup output Restore last (GeneratorStartup). True: started.
    bool restoreStartupOutput(const PreferencesResult &applied);
    // A user's output ON/OFF (the key, OUTP): the one path, so Startup output keeps what the user asked
    // once the driver confirms it. True when the output is in the state asked.
    bool requestGeneratorOutput(bool on, uint32_t now);
    // The confirmed state of a user's request into the preferences (each pass and before any settings
    // save, so a save right after OUTP ON already has it).
    void followGeneratorOutput(uint32_t now);
    enum class ScopeApply : uint8_t { Configure, StageIfHeld, Resume };
    bool applyScope(const ScopeRequest &request, uint32_t now, ScopeApply mode = ScopeApply::Configure);
    bool applyFollowing(ScopeRequest request, uint32_t now, ScopeApply mode);
    bool applyEdited(ScopeRequest request, uint32_t now);
    bool changePlotMode(plot::Options &options, plot::Mode before, uint32_t now);
    bool channelShown(unsigned channel);
    // Moves the scale/position selection off a channel that is not shown; true when it moved.
    bool followShownChannel();
    void panelKeys(uint32_t keys, uint32_t now, bool fine);
    void setScopeFocus(plot::Focus focus);
    void syncCursorFocus();
    void openAnalyze(unsigned row);
    void enterCursors(uint32_t now);
    void showTrigger(const scope::FrameMetadata *shown);
    void beginHeldView();
    void followNextTimebase(); // A held record is shown at the next capture's time/div (issue #7).
    void moveTrace(uint32_t keys, uint32_t now, bool fine);
    void resetPan(uint32_t now); // In HOLD the explored view only, as moveTrace: RUN restores, nothing saved.
    void moveCursor(uint32_t keys, uint32_t now, bool fine);
    bool triggerMidpoint(uint32_t now);
    bool centerChannels(uint32_t now); // Analyze -> Center channels, F2 hold.
    bool applyGenerator(const generator::Settings &settings, uint32_t now);
    void dataKeys(uint32_t keys, uint32_t now, bool fine);
    void displayData(uint32_t now);
    void plotKeys(uint32_t keys, uint32_t now, bool fine);
    void scopeKeys(uint32_t keys, uint32_t now, bool fine);
    void meterKeys(uint32_t keys, uint32_t now);
    void meterRangePress(uint32_t now); // AUTO/RANGE's qualified press on the DMM screen: the stroke's 20.
    bool meterRangeContext() const;     // The DMM screen takes AUTO/RANGE (no panel, menu, Remote).
    void meterRangeKeys(const KeyEvents &events, uint32_t now); // AUTO/RANGE's press, release, hold in the DMM.
    void serviceMeterRange(); // Closes the open AUTO/RANGE stroke once the DMM screen no longer has the key.
    void applyBrightness();
    void shutdownNotice(const char *text, uint32_t now);
    void generatorKeys(uint32_t keys, uint32_t now);
    void triggerKeys(uint32_t keys, uint32_t now);
    void frequencyKeys(uint32_t keys, uint32_t now);
    void meterPanelKeys(uint32_t keys, uint32_t now);
    void meterChanged(unsigned row, uint32_t now); // The DMM panel's changed row, taken at once (that row only).
    ui::MeterProperties meterProperties() const;
    void meterAppearanceKeys(uint32_t keys, uint32_t now);
    // Measurement > Hold: each committed field saved and applied at once (a running cycle ends, none
    // starts); Back: the DMM panel on its Hold row.
    void meterHoldKeys(uint32_t keys, uint32_t now);
    // The one path for new hold settings (the panel, SCPI): valid ones saved and applied (a running
    // cycle ends, none starts); the same ones change nothing.
    bool meterHoldSettings(const dmm::HoldSettings &settings, uint32_t now);
    // The HOLD key in the DMM by the mode (docs/DMM_AUTO_HOLD.md): a manual or the DMM's own HOLD
    // released first; Manual: HOLD now; the others: the cycle armed, or (armed) cancelled.
    void meterHoldKey(uint32_t now);
    // LAYOUT (Scope, DMM digital or analog) and the Scope's COLORS: each committed choice is taken
    // and saved at once and only restyles the screen (applyLooks); the capture, HOLD, the record,
    // the DMM statistics and the measurement settings are untouched. Back: the parent panel.
    void scopeLayoutKeys(uint32_t keys, uint32_t now);
    // GENERATOR LAYOUT, one editor from Settings / Display and from GEN Parameters (F3); Back returns
    // to where it opened. A committed order is saved and placed at once, or when GEN shows next.
    void openGeneratorLayout(bool fromSettings);
    void generatorLayoutKeys(uint32_t keys, uint32_t now);
    void scopeColorsKeys(uint32_t keys, uint32_t now);
    void meterLayoutKeys(uint32_t keys, uint32_t now);
    template<class Block> bool layoutKeys(uint32_t keys, ui::ArrangementProperties<Block> &editor, BlockOrder<Block> &field, uint32_t now);
    void openMeterLayout();
    void applyLooks();  // The saved block orders and Scope colours to the view.
    void buildView();   // The view rebuilt over the theme in force, with applyLooks.
    void refreshMeterPanel();
    void analyzeKeys(uint32_t keys, uint32_t now);
    // The protocol decoder (docs/SCOPE_PROTOCOL_DECODERS_PLAN.md; decoder_keys.cpp). Its job and
    // result sets live in the ITCM workspace (decode/workspace.hpp), not in this object.
    bool decoderScreen() const; // Split or Decoder only in front: the decoder's own keys (§4.4).
    void decoderKeys(uint32_t keys, uint32_t now);
    void decoderPanelKeys(uint32_t keys, uint32_t now); // Panel::Decoder (Analyze > Decoder, F1, F2).
    void openDecoder(unsigned row);
    void decoderSummary(); // Save CSV's and Detect baud's values in the panel.
    void baudAction(uint32_t now);                         // Detect baud: search the held record, or apply its hint.
    bool baudHintReady() const;                            // A confirmed hint for the settings in force.
    void baudHintText(char *out, size_t capacity) const;
    void applyDecoder(const decoding::Settings &settings, uint32_t now); // Saved, the job and the view follow.
    void configureDecoder(); // prefs_' decoder to the job; Off: no job, no revocation callback.
    bool scopeFrame(scope::FrameLease &frame); // The record the trace shows next (displayData).
    void feedDecoder();  // Without a trace: the newest record to the job.
    void noteSkipped();  // A record not decoded while one runs: counted once (looked at, not taken).
    void showDecoder(uint32_t now); // The view's decoder state and results (each display pass).
    void jumpToEvent(uint32_t now); // F3: the held trace at the selected event.
    // Stage 7 (decode_save.cpp): the published result as /openosc/DECODE/DECnnnnn.CSV (menu, DECode:SAVE):
    // 0 started, -200 busy or no storage, -230 no finished result; written a slice a loop pass.
    int16_t saveDecoderCsv(uint32_t now);
    void serviceDecoderCsv(uint32_t now);
    const char *decoderCsvSaved() const; // DECode:SAVE?: "BUSY", the last file, or why it failed.
    const decoding::ResultSet *decoderResult() const; // The result on show (table, strip, keys).
    void serviceDecoder(uint32_t now); // One bounded slice of the record being decoded (the main loop).
    // The decoded record kept for USB hosts (DecodeJob's host share): shared while a host asks for
    // records; dropped once FrameHosts owes it to none (every host took it, DemandMs, HoldMs, session end).
    void syncHostRecord(uint32_t now);
    bool holdKeys(uint32_t keys, uint32_t now); // HOLD/SAVE: RUN <-> HOLD, as on every Scope screen.
    decoding::DecodeJob &decoder_ = decoding::workspaceJob();
    ui::DecoderProperties decoderEditor_;
    uint16_t decoderRow_ = 0, decoderLine_ = 0; // The selected event; Detail's first line shown.
    bool decoderDetail_ = false;                  // MENU on a row: its Detail.
    decoding::RecordRate decoderRate_;
    void scopeEditorKeys(uint32_t keys, uint32_t now);
    void measurementKeys(uint32_t keys, uint32_t now);
    void generatorEditorKeys(uint32_t keys, uint32_t now);
    // The generator's three panels from the GEN screen and from each other (generator_editor_keys.cpp):
    // F1 WAVEFORM, F2 FREQUENCY, F3 PARAMETERS. Leaving a panel applies its field in progress first;
    // refused, the panel stays with its draft and says so. True when the keys were such a switch.
    void openGeneratorEditor(bool wave);
    void openFrequencyEditor();
    bool switchGeneratorPanel(uint32_t keys, uint32_t now);
    bool finishGeneratorField(uint32_t now); // False: refused, nothing applied, the panel stays.
    void generatorPanelKeys(uint32_t keys, uint32_t now);  // generatorEditorKeys' work, before the captions.
    void frequencyPanelKeys(uint32_t keys, uint32_t now);  // frequencyKeys' work, before the captions.
    // F1-F4's captions for what is in front now (ui::SoftKeys), from the state the key handlers read:
    // after keys and on every display pass, so background updates never leave other captions.
    void refreshSoftKeys();
    ui::SoftKeys imageSoftKeys() const; // The image list's and preview's keys (also the preview's header line).
    void startAuto(uint32_t now);
    void serviceAuto(uint32_t now);
    bool cancelAuto(uint32_t now,bool hold=false);
    bool autoBusy() const { return autoSetup_.state()!=scope::AutoSetup::State::Idle; }
    bool restoreAuto();
    void serviceInstruments(uint32_t now);
    void queuePlotSave(uint32_t now);
    enum class Panel { None, Display, Trigger, Frequency, Meter, MeterAppearance, MeterHold, MeterLayout, ScopeLayout, ScopeColors, Analyze, Decoder, Channel, Acquisition, Measurements, Generator, GeneratorLayout, Confirmation, Images, Zero };
    Panel panel_ = Panel::None;
    // Every property panel and Settings: one navigator (only one is open at a time).
    ui::PropertyNavigator navigator_;
    void openPanel(Panel panel, const ui::PropertyList &list, unsigned row = 0);
    void closePanel();
    void backPanel(); // Back: an Analyze sub-menu returns to Analyze on its row, any other panel closes.
    unsigned analyzeRow_ = ~0U; // The Analyze row the open sub-menu came from (~0: none).
    // Settings or a panel left by another key than its own (MODE, Remote, power off): an edit ends
    // as on F4. A Settings number (brightness, applied live) returns to the value it began with;
    // a panel's unfinished number goes with the panel (its steps were never applied). True if a
    // Settings value was restored.
    bool dropEdit(uint32_t now);
    // The keys of a property panel, field by field (PropertyNavigator): a committed field
    // (Committed) or an action row (Action) is the caller's to apply; F4/Left in Browse close the
    // panel, keeping every field committed before. A number's steps are only redrawn.
    enum class FieldKey : uint8_t { None, Committed, Action, Closed };
    FieldKey fieldKeys(uint32_t keys, ui::PropertyList &list, uint32_t now);
    // The Channel/Acquisition editor's committed field to the instrument; the editor then shows what
    // the instrument took (or, refused, what it has).
    void commitScopeEditor(ui::ScopeEditor &editor, uint32_t now);
    void commitTrigger(uint32_t now);
    void triggerModeStep(uint32_t now); // F3 again in Trigger focus: Auto, Normal, Single.
    // DISPLAY's committed field (row Count) or its Reference/Clear action row, with every draft;
    // refused, the drafts return to the view in force.
    bool applyDisplay(unsigned row, uint32_t now);
    // What a Settings choice changed; runModeChosen: the Run mode row itself was committed (RunModeChoice).
    void applySettings(const Preferences &chosen, uint32_t now, bool runModeChosen = false);
    ui::TriggerEditor triggerEditor_;
    ui::FrequencyEditor frequencyEditor_;
    ui::ChannelEditor channelEditor_;
    ui::AcquisitionEditor acquisitionEditor_;
    ui::MeasurementEditor measurementEditor_;
    ui::GeneratorEditor generatorEditor_;
    scope::AutoSetup autoSetup_;
    ReadoutScheduler readouts_;
    bool autoWasHeld_ = false;
    bool autoCancelled_ = false;
    bool recordWatched_ = false, loggerWatched_ = false, serialWatched_=false;
    MeterRestore meterRestore_; // The DMM entry's range restore (meter_restore.hpp).
    // REL hold (Preferences::holdMs) and Scope AUTO hold (AutoHoldMs): AUTO fires auto setup; its release then is not a short press.
    static constexpr uint32_t AutoHoldMs = 2000;
    uint32_t autoHeldAt_ = 0, autoUpAt_ = 0;
    bool powerFired_ = false, autoDown_ = false, autoFired_ = false, autoArmed_ = false;
    bool autoShown_ = false; // The AUTO countdown appeared: its release is a cancelled hold, not a short AUTO.
    void powerOff(uint32_t now);
    int autoHold(uint32_t now, bool powerHeld); // PowerHold::percent of the AUTO hold.
    // An explicit DMM command (keys, SCPI): accepted, it supersedes the entry's pending restore.
    bool meterCommand(bool accepted) { if (accepted) meterRestore_.cancel(); return accepted; }
    // *OPC?'s view of that restore (InstrumentBackend::busy): owed and still able to happen.
    bool meterRestorePending() const { return meterRestore_.pending(data_.measurementMode(), data_.dmmSelectionStatus()); }
    DataStatus lastSelection_ = DataStatus::NoData;
    uint32_t generatorFaults_ = 0; // generator::Snapshot::faults as last reported (one toast per stop).
    ui::MeterProperties meterEditor_{dmm::Result{}};
    ui::MeterAppearanceProperties appearanceEditor_;
    ui::MeterHoldProperties holdEditor_;
    ui::ArrangementProperties<ScopeBlock> scopeLayout_;
    ui::ArrangementProperties<GeneratorBlock> generatorLayout_;
    bool generatorLayoutFromSettings_ = false; // Where Back returns: Settings / Display, or GEN Parameters.
    ui::ArrangementProperties<DigitalBlock> digitalLayout_;
    ui::ArrangementProperties<AnalogBlock> analogLayout_;
    bool meterLayoutAnalog_ = false; // Which DMM order the open LAYOUT edits (the face shown when it opened).
    plot::Focus focus_ = plot::Focus::Scale;
    plot::CursorControl cursors_;
    scope::TriggerFollow triggerFollow_;
    plot::Controls plotControls_;
    void render();
    void prepareColumns();
    scope::FrameLease ahead_; // The next frame, looked at during one refresh (prepareColumns).
    bool rendering_ = false;  // Inside render(): not lv_refr_now of a screenshot.
    LvglDisplay &display_;
    NorFlash &nor_;
    Filesystem &fs_;
    PreferencesFile &preferences_;
    ThemeStore &themes_;
    ScreenshotWriter &shots_;
    DataAccess &data_;
    // Over the theme in force (ThemeStore's, reloaded in place): Default shows its real colours.
    ui::ScopeAppearanceProperties colorsEditor_{themes_.theme()};
    ApplicationState state_;
    Preferences prefs_;
    GeneratorStartup startup_; // Startup output: the user's last ON/OFF request awaiting confirmation.
    bool captureHalvedNoticed_ = false; // The DAC stream's halved capture maximum was said (displayData).
    ApplicationView view_;
    screenshot::ImageCatalog images_;
    screenshot::ImageReader imageReader_;
    bool imageShowing_=false, imageFullSize_=false;
    void startScreenshot(uint32_t now);
    unsigned shotProgress_=0;
    unsigned imageProgress_=0;
    ui::SettingsMenu settingsMenu_;
    DeviceInfo settingsInfo_;
    PendingSaves saves_; // Theme/plot and settings files to write (background or once at shutdown).
    BootGuard *guard_ = nullptr;                // Without it (tests) the 360 MHz profile is refused.
    BootGuard::Start bootStart_{0, false, nullptr};
    BootConfirmation guardConfirmation_;
    bool started_ = false;                      // UI and measurement backend came up: may confirm 360.
    void serviceBootGuard(uint32_t now);
    bool guardStopped_ = false;                 // "stopped" written by a shutdown that may still be refused.
    void stopBootGuard();                       // A deliberate exit from an unconfirmed 360 MHz run.
    const char *guardNotice_ = nullptr;         // After a refused restart: what became of 360 MHz.
    class RefusedRestartSteps;
    // The files PendingSaves writes, as this instrument writes them (controller.cpp).
    class SettingsStore final : public PendingSaves::Store {
    public:
        explicit SettingsStore(ApplicationController &a) : a_(a) {}
        bool saveTheme() override;
        bool savePreferences() override;
    private:
        ApplicationController &a_;
    };
    static constexpr uint32_t OverlaySettleMs = 60;
    uint32_t hiddenAt_ = 0xffff0000U;
    uint32_t fpsAt_ = 0, fpsFlushes_ = 0, fps_ = 0;
    double counterRate_ = -1; bool counterStale_ = true; // The top bar's counter as last drawn.
    uint32_t dataAt_ = 0, lastScopeSequence_ = 0, lastMeterSequence_ = 0;
    uint32_t meterBarAt_ = 0;
    unsigned selectedChannel_ = 0;
    bool dataRefresh_ = true;
    generator::Snapshot lastGenerator_;
    DataStatus lastMeterStatus_ = DataStatus::NoData;
    DataStatus lastScopeStatus_ = DataStatus::NoData;
    bool lastMeterHold_ = false, lastScopeHold_ = false;
    char lastMeterHoldWord_[16] = ""; // The hold mode's state pill word as last shown.
    void meterHoldWord(char *output, size_t capacity) const; // "" under the manual/DMM HOLD or idle.
    plot::Options heldView_;
    bool heldViewActive_ = false;
    uint32_t heldViewRelease_ = 0;
};
} // namespace openosc
