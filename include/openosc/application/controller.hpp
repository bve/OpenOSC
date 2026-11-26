#pragma once
#if OPENOSC_BOARD == 703
#include "openosc/calibration/interleave_match_wizard.hpp"
#endif
#include "openosc/application/generator_startup.hpp"
#include "openosc/application/meter_restore.hpp"
#include "openosc/application/state.hpp"
#include "openosc/application/preferences_result.hpp"
#include "openosc/input/gestures.hpp"
#include "openosc/instrument/data_access.hpp"
#include "openosc/application/measurement_wake.hpp"
#include "openosc/usb/auto_policy.hpp"
#include "openosc/screenshot/writer.hpp"
#include "openosc/screenshot/catalog.hpp"
#include "openosc/application/awg_files.hpp"
#include "openosc/screenshot/reader.hpp"
#include "openosc/settings/preferences_file.hpp"
#include "openosc/settings/theme_store.hpp"
#include "openosc/ui/application_view.hpp"
#include "openosc/ui/lvgl_display.hpp"
#include "openosc/ui/editors/trigger_editor.hpp"
#include "openosc/ui/editors/meter_properties.hpp"
#include "openosc/ui/editors/meter_appearance_properties.hpp"
#include "openosc/ui/editors/meter_hold_properties.hpp"
#include "openosc/ui/editors/meter_log_properties.hpp"
#include "openosc/application/session_browser.hpp"
#include "openosc/ui/editors/arrangement_properties.hpp"
#include "openosc/ui/editors/scope_appearance_properties.hpp"
#include "openosc/ui/editors/scope_editor.hpp"
#include "openosc/ui/editors/decoder_properties.hpp"
#include "openosc/decode/workspace.hpp"
#include "openosc/decode/record_rate.hpp"
#include "openosc/decode/table_text.hpp"
#include "openosc/application/readout_scheduler.hpp"
#include "openosc/application/run_mode_switch.hpp"
#include "openosc/application/scope_auto_key.hpp"
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
#include "openosc/platform/mcu_temperature.hpp"
#include "openosc/application/stall_log.hpp"
#include "openosc/application/screen_lease.hpp"
#include "openosc/ui/scope/counter_text.hpp"

namespace openosc {
class InstrumentBackend;
class InstrumentUsb;
class ApplicationController {
public:
    ApplicationController(LvglDisplay &display, NorFlash &nor, Filesystem &fs, PreferencesFile &preferences,
                          ThemeStore &themes, ScreenshotWriter &shots, DataAccess &data,
                          BatteryMonitor &battery, McuTemperature &mcuTemperature)
        : battery_(battery), mcuTemperature_(mcuTemperature), display_(display), nor_(nor), fs_(fs),
          preferences_(preferences), themes_(themes), shots_(shots), data_(data), images_(fs), imageReader_(fs) {}
    // The 360 MHz guard and what the boot made of it (main.cpp, before run()).
    void bootGuard(BootGuard &guard, const BootGuard::Start &start) { guard_ = &guard; bootStart_ = start; }
    [[noreturn]] void run();
    enum class ModeChange : uint8_t { Selected, Busy, Refused };

private:
    friend class InstrumentBackend;
    friend class ScopeControllerProbe; // Native tests drive the key handlers directly.
    bool setRemote(bool enabled, const char *left = nullptr); // left: the notice when Remote ends cleanly.
    ModeChange selectMode(MeasurementMode next, uint32_t now); // MODE key and SYST:MODE.
    InstrumentUsb *usb_ = nullptr;
    // remote_: USB Remote control (keys locked except MENU/REL hold exits, no idle power-off);
    // the display keeps running. uiReady_: the LVGL UI exists — every view update, render and
    // notification depends on it; false only for the headless fallback of a failed display init.
    bool remote_ = false, uiReady_ = false;
    StallLog stalls_;
    plot::CounterText counterText_;
    uint32_t counterEpoch_ = 0; // Each instrument mode change: the counter overlay's mean starts again.
    bool sectionDirect_ = false; // A Display section opened from Analyze (FFT): Back returns there.
    plot::Focus zoomReturn_ = plot::Focus::Scale; // The card Zoom & Pan returns to (F4) and takes MENU from.
    uint16_t autoStep_ = 0; // The Scale arrows that cancelled Auto Setup, until its restore succeeds.
    static constexpr unsigned PanDoubling = 8;
    unsigned panRepeats_ = 0;
    int panSign_ = 0; // The direction panRepeats_ counts for.
    uint8_t triggerRepeats_ = 0;
    int8_t triggerSign_ = 0;
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
    char gainQuestion_[32]{}; // The gain confirmation's title (Confirmation keeps a pointer).
    void serviceZeroUi(uint32_t now);
    // Scope calibration > Interleave offset (ZT703S): its start after the confirmation, its result.
    void interleaveOffsetAction(uint32_t now); // MENU on its row: confirm, or cancel a running one.
    void beginInterleaveOffset(uint32_t now);
    // Interleave CH1 (ZT703S) from the menu or SCPI: applied at once and saved; false when refused.
    bool selectInterleave(scope::InterleaveMode mode, uint32_t now);
    void serviceInterleaveOffsetUi(uint32_t now);
    // Scope calibration > Gain > Match to other channel (GitHub #80, gain_match.cpp).
    void openGainMatch(uint32_t now);
    // Out of line: its rows would grow onKeys' frame on the deepest key chain (stack budget).
    [[gnu::noinline]] void gainMatchKeys(uint32_t keys, uint32_t now);
    void serviceGainMatchUi(uint32_t now);
    void finishGainMatch(uint32_t now, Tooltip kind, const char *notice, bool returnToSettings = true);
    void showGainMatch();
#if OPENOSC_BOARD == 703
    void openInterleaveMatch(uint32_t now);
    void interleaveMatchKeys(uint32_t keys, uint32_t now);
    void serviceInterleaveMatchUi(uint32_t now);
    void finishInterleaveMatch(uint32_t now, Tooltip kind, const char *notice, bool returnToSettings = true);
    void showInterleaveMatch();
    calibration::InterleaveMatchWizard interleaveMatch_;
#endif
    // Frequency response (GitHub #72, bode.cpp; ZT703S and ZT706S): its session lives in ITCM
    // (BodeSession), none here.
    void openBode(uint32_t now);
    void startBodeSweep(uint32_t now);
    void saveBodeCsv(uint32_t now); // Setup's Save CSV: the last run as it ran (BodeCsvFile).
    void bodeKeys(const KeyEvents &events, uint32_t keys, uint32_t now);
    void serviceBode(uint32_t now);
    void finishBode(uint32_t now, Tooltip kind, const char *notice); // Cancel, restore the instrument, close the panel.
    void showBode(bool plot);
    bool bodeRunning() const;
    bool bodeOwns() const; // The sweep's settings in force: running, or their restore pending (SCPI refused).
    bool bodeRestoring() const; // Not running, its restore not yet whole (a ZT706S acknowledge): no new sweep.
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
    // `measured`: an instrument's own event (measurementActivity) counts as activity as a key does,
    // without waking a key (wakeTakesKey) and never against a low-battery or other shutdown.
    bool servicePower(uint32_t keys,uint32_t now,bool measured=false);
    // Settings > Files & USB > USB controller (usb_auto.cpp, GitHub #170): applyUsbController puts the
    // chosen mode in force once (boot, reload, Settings); serviceUsbAuto runs Auto's policy each pass.
    void applyUsbController(uint32_t now);
    void serviceUsbAuto(uint32_t now);
    bool usbAutoOff() const; // Auto with the controller off: no computer found (Remote is refused).
    usb::AutoPolicy usbAuto_;
    uint8_t usbApplied_ = 0xff; // The mode in force (none before the boot applies it).
    // GitHub #169, Settings > Power > Wake on measurement: once a pass, whether the instrument on show
    // had an event of its own (MeasurementWake: a DMM reading that moved, a Scope record of a real
    // trigger). AUTO, a calibration, a Bode sweep or a shutdown: none.
    bool measurementActivity();
    MeasurementWake measurementWake_;
    IdlePolicy idle_;
    BatteryMonitor &battery_;
    McuTemperature &mcuTemperature_;
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
    // Follow: a request whose trigger level stands (a scale, a zero, anything else) keeps the trigger's
    // voltage; Set: its level is set on purpose and becomes the anchor, even as the same whole code.
    enum class LevelIntent : uint8_t { Follow, Set };
    bool applyFollowing(ScopeRequest request, uint32_t now, ScopeApply mode, LevelIntent intent = LevelIntent::Follow,
                        scope::TriggerLevel::Bounds bounds = {});
    static scope::TriggerFollow::Place triggerPlace(const ScopeRequest &request); // TriggerFollow's key.
    bool applyEdited(ScopeRequest request, uint32_t now, LevelIntent intent = LevelIntent::Follow);
    bool applyFound(const ScopeRequest &request, uint32_t now); // AUTO's result: its level is set (LevelIntent::Set).
    bool changePlotMode(plot::Options &options, plot::Mode before, uint32_t now);
    bool channelShown(unsigned channel);
    // Moves the scale/position selection off a channel that is not shown; true when it moved.
    bool followShownChannel();
    void panelKeys(uint32_t keys, uint32_t now, bool fine);
    void setScopeFocus(plot::Focus focus);
    void syncCursorFocus();
    void openAnalyze(unsigned row);
    void enterCursors(uint32_t now);
    void focusCursors(); // The Cursor card on its first line drawn, nothing changed or saved (GitHub #125).
    void showTrigger(const scope::FrameMetadata *shown);
    void beginHeldView();
    void followNextCapture(); // A held record is shown at the next capture's time/div and V/div (issues #7, #93).
    void moveTrace(uint32_t keys, uint32_t now, bool fine);
    // Pan and zoom change the view of the Y-T record on show: false when no view of it can follow.
    bool viewFollows() const;
    void resetPan(uint32_t now); // In HOLD the explored view only, as moveTrace: RUN restores, nothing saved.
    // The Scope waveform screen in front with its own keys: no panel, Settings, decoder screen, Remote,
    // Bode restore, shutdown or Recovery handoff. AUTO and the F1-F4 holds act only there (issue #96).
    bool scopeScreen() const;
    // AUTO's strokes there (ScopeAutoKey), by Settings > Display > Scope shortcuts > AUTO key.
    // Out of line, as the holds below: their frames stay off onKeys' (the deepest run() chain, stack budget).
    [[gnu::noinline]] void scopeAutoKeys(const KeyEvents &events, uint32_t now);
    // Each loop pass: the AUTO countdown (PowerHold::percent), -1 without one; REL held or the screen
    // gone ends the stroke. scopeAutoHint: the countdown's text.
    [[gnu::noinline]] int scopeAutoCountdown(uint32_t now, bool powerHeld);
    const char *scopeAutoHint() const;
    // F1-F4 held to their long press on that screen (issues #95, #96): Probe, the F2 reset, 50 %, the
    // F4 shortcut. Once a stroke (KeyGestures). True when the screen took the holds.
    [[gnu::noinline]] bool scopeHoldKeys(uint32_t holds, uint32_t now);
    void openChannel(unsigned row = ui::ChannelEditor::Channel);
    void openProbe(uint32_t now);    // F1 hold: the selected channel's Probe choices.
    void openShortcut(uint32_t now); // F4 hold: the saved destination's editor, nothing changed by opening it.
    bool resetView(uint32_t now);    // F2 hold: Home view or Reset zoom & pan; explicit, so RUN keeps it.
    // Zoom 1x and the pan at the trigger reference, saved; also in the view RUN returns to from HOLD.
    void homeZoom(uint32_t now);
    // Zoom & Pan (issue #96, ViewZoom): a focus of its own; F4 returns to the card it came from.
    void enterZoom(uint32_t now);
    void leaveZoom();
    void syncZoomFocus(); // Leaves Zoom & Pan once the view has no Y-T record to zoom.
    void zoomKeys(uint32_t arrows, uint32_t now, bool fine);
    // F1's Up/Down V/div and Left/Right time/div step (in HOLD staged for RUN).
    void scaleKeys(uint32_t keys, uint32_t now);
    // A Scale arrow during Auto Setup cancels it; its step (autoStep_) is applied once, after the
    // restore succeeds (at once, or when serviceAuto completes a deferred one).
    void finishAutoStep(uint32_t now);
    bool centerTrigger(uint32_t now);
    void moveCursor(uint32_t keys, uint32_t now, bool fine);
    // The level at 50 % of the newest record's channel; with `pretrigger` (F3 held, issue #123) also the
    // pretrigger at 50 %, set even when no record gives a midpoint.
    bool triggerMidpoint(uint32_t now, bool pretrigger = false);
    // The Trigger card's Left/Right (issue #123): the next captures' pretrigger.
    void movePretrigger(uint32_t arrows, uint32_t now, bool fine);
    scope::TriggerLevel::Bounds triggerBounds(const ScopeRequest &request);
    int triggerStep(int direction, bool fine);
    // The level at the upper (+1) or lower (-1) end of triggerBounds on the screen (the channel's
    // invert turns them over), else 0; outside Y-T or in Roll none (issue #61).
    int triggerLimit(const ScopeRequest &request);
    // A press against that end: the marker's pulse (WaveformWidget::triggerPulse), no notice.
    void pulseTriggerLimit(uint32_t now) { if (uiReady_) view_.waveform().triggerPulse(now); }
    // The Trigger panel's Level row marks a level at either end of its range: the panel can cover
    // the plot and its draft is not applied before MENU. True when that mark changed.
    bool markTriggerEditor();
    bool centerChannels(uint32_t now); // Analyze -> Center channels, and the F2 hold's Home view.
    bool applyGenerator(const generator::Settings &settings, uint32_t now);
    // A staged Arbitrary waveform taken (SOUR:ARB:DATA, the CSV loader: docs/AWG.md): true when it is the
    // loaded one now (playing at once while Arbitrary is on), false when the stage was lost or the
    // start refused (the previous waveform stays).
    bool commitArbitrary(const void *owner, unsigned points, generator::ArbitrarySource source, uint32_t now);
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
    void triggerKeys(uint32_t keys, uint32_t now, bool fine);
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
    bool meterTemperatureSettings(dmm::TemperatureSettings settings, uint32_t now);
    // Measurement > Session log (GitHub #144, meter_log.cpp): its panel, the start/stop action, the saved
    // sessions' list and trend. The browser (SessionBrowser) exists only while the list is open: it is
    // allocated from the LVGL heap then, nothing of it in this object's frame.
    ui::MeterLogProperties meterLogProperties() const;
    void openMeterLog(unsigned row = ui::MeterLogProperties::Action);
    void meterLogKeys(uint32_t keys, uint32_t now);
    void meterLogAction(uint32_t now);
    // Out of line, as the other per-pass services' buffers: off run()'s frame (the deepest chain, stack budget).
    [[gnu::noinline]] void serviceMeterLog(uint32_t now); // The session's end said; the open panel's clock; the browser's reading.
    void openSessions(uint32_t now);
    void closeSessions(bool toPanel);   // toPanel: back to Session log on its Saved sessions row.
    [[gnu::noinline]] void sessionKeys(const KeyEvents &events, uint32_t keys, uint32_t now);
    void showSessions();
    void deleteSession(uint32_t now);
    bool sessionsBrowsing() const { return sessions_ != nullptr; } // The LOGS files are being read (MMEM:DEL waits).
    ui::SoftKeys sessionSoftKeys() const;
    SessionBrowser *sessions_ = nullptr;
    bool sessionWatched_ = false, confirmationSessions_ = false;
    // GEN > Waveform > AWG file (ZT703S, docs/AWG.md): the /openosc/AWG list and its file's load in
    // slices (awg_keys.cpp). The browser exists only while its list is open (LVGL heap).
    void beginGeneratorEditor(bool wave); // The panel's draft and the loaded waveform it shows.
    void openAwgFiles(uint32_t now);
    void closeAwgFiles(bool toPanel);     // toPanel: back to WAVEFORM on its AWG file row.
    void showAwgFiles();
    void awgFileKeys(uint32_t keys, uint32_t now);
    void loadAwgFile(uint32_t now);
    [[gnu::noinline]] void serviceAwgFiles(uint32_t now);
    ui::SoftKeys awgSoftKeys() const;
    AwgFiles *awgFiles_ = nullptr;
    char awgFile_[AwgFiles::NameBytes]{}; // The file loaded last, while awgRevision_ is the loaded waveform's.
    uint32_t awgRevision_ = 0;
    uint32_t logShownAt_ = 0;
    void serviceMcuTemperature(uint32_t now);
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
    void recordResult(DataStatus status, char *text, size_t capacity); // Analyze -> Save RAW / CSV's toast.
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
    enum class Panel { None, Display, Trigger, Frequency, Meter, MeterAppearance, MeterHold, MeterLog, Sessions, MeterLayout, ScopeLayout, ScopeColors, Analyze, Decoder, Channel, Acquisition, Measurements, Generator, GeneratorLayout, AwgFiles, Confirmation, Images, Zero, InterleaveMatch, GainMatch, Bode };
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
    void commitTrigger(uint32_t now, bool level); // level: the Level row or 50 % (LevelIntent::Set).
    void triggerModeStep(uint32_t now); // A short F3 on the Trigger card: Auto, Normal, Single (held: staged).
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
    uint32_t dmmRestarts_ = 0, dmmReselects_ = 0;
    bool powerFired_ = false; // The REL hold (Preferences::holdMs) powered off once this stroke.
    ScopeAutoKey autoKey_;    // AUTO's open stroke on the Scope waveform screen.
    void powerOff(uint32_t now);
    // An explicit DMM command (keys, SCPI): accepted, it supersedes the entry's pending restore.
    bool meterCommand(bool accepted) { if (accepted) meterRestore_.cancel(); return accepted; }
    // *OPC?'s view of that restore (InstrumentBackend::busy): owed and still able to happen.
    bool meterRestorePending() const { return meterRestore_.pending(data_.measurementMode(), data_.dmmSelectionStatus()); }
    DataStatus lastSelection_ = DataStatus::NoData;
    uint32_t generatorFaults_ = 0; // generator::Snapshot::faults as last reported (one toast per stop).
    ui::MeterProperties meterEditor_{dmm::Result{}};
    ui::MeterAppearanceProperties appearanceEditor_;
    ui::MeterHoldProperties holdEditor_;
    ui::MeterLogProperties logEditor_;
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
    // DISP:DATA? streams the screen from the preview buffer under a lease ticket (ScreenLease).
    ScreenLease screenLease_;
    void startScreenshot(uint32_t now);
    void finishScreenshot(uint32_t now);
    unsigned shotProgress_=0;
    unsigned imageProgress_=0;
    ui::SettingsMenu settingsMenu_;
    DeviceInfo settingsInfo_;
    PendingSaves saves_; // Theme/plot and settings files to write once, at shutdown.
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
    static constexpr uint32_t UnusedMs = 10000; // No key this long: the block cache may flush (BlockDevice::service).
    uint32_t hiddenAt_ = 0xffff0000U;
    uint32_t fpsAt_ = 0, fpsFlushes_ = 0, fps_ = 0;
    double counterRate_ = -1; bool counterStale_ = true; // The top bar's counter as last drawn.
    // The run profile as the top bar's glyph last read it: ClockControl::activeMode() again only when
    // ClockControl::changes() moved (a change, its rollback or fallback, Remote), never each pass.
    uint32_t clockChanges_ = ~0U, clockMode_ = RunModeCount;
    // No factory record: once on each entry to the Scope (the boot's included), when no other notice is on
    // show, that the scale is the factory's defaults (nominalNotice). scopeShown_: the Scope last pass.
    bool nominalNoticeDue_ = false, scopeShown_ = false;
    void nominalNotice(uint32_t now);
    uint32_t dataAt_ = 0, lastScopeSequence_ = 0, lastMeterSequence_ = 0;
    uint32_t meterBarAt_ = 0;
    unsigned selectedChannel_ = 0;
    bool dataRefresh_ = true;
    generator::Snapshot lastGenerator_;
    DataStatus lastMeterStatus_ = DataStatus::NoData;
    DataStatus lastScopeStatus_ = DataStatus::NoData;
    bool lastMeterHold_ = false, lastScopeHold_ = false;
    dmm::Function lastMeterSelecting_ = dmm::Function::Unknown;
    double lastMeterCharge_ = 0;
    dmm::Function meterSelecting() const;
    char lastMeterHoldWord_[16] = ""; // The hold mode's state pill word as last shown.
    const char *saveProblem_ = nullptr, *saveWarned_ = nullptr; // store(): the power-off save's volume, its notice.
    uint32_t saveCheckedAt_ = 0U - 60000U, saveWarnedAt_ = 0U - 600000U; // Due at once if changes wait from boot.
    bool saveShownPending_ = false; // The open Settings page's status line, as drawn.
    bool saveSettingsOpen_ = false; // store(): settingsOpen at the last pass (a recheck when Settings opens).
    void meterHoldWord(char *output, size_t capacity) const; // "" under the manual/DMM HOLD or idle.
    plot::Options heldView_;
    bool heldViewActive_ = false;
    uint32_t heldViewRelease_ = 0;
};
} // namespace openosc
