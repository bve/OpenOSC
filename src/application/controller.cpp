#include "openosc/application/boot_timing.hpp"
#include "openosc/platform/watchdog.hpp"
#include "openosc/application/controller.hpp"
#include "openosc/platform/stack_guard.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/input_service.hpp"
#include "openosc/platform/cooperative_service.hpp"
#include "openosc/ui/render_idle.hpp"
#include "openosc/ui/scope/column_cache.hpp"
#include "openosc/platform/power.hpp"
#include "openosc/platform/clocks.hpp"
#include "openosc/application/scpi_backend.hpp"
#include "openosc/application/power_hold.hpp"
#include "openosc/usb/instrument_usb.hpp"
#ifdef OPENOSC_DIAG_MAILBOX
#include "openosc/application/local_control.hpp"
#endif
#ifdef OPENOSC_INTERLEAVE_PROBE
#include "openosc/application/interleave_probe_control.hpp"
#endif
#include <cstdio>

namespace openosc {
void ApplicationController::serviceMcuTemperature(uint32_t now) {
    auto &meter = data_.meter();
    const bool enabled = data_.measurementMode() == MeasurementMode::Multimeter &&
                         prefs_.meter.temperature.source == dmm::JunctionSource::McuLive;
    const auto reading = mcuTemperature_.poll(now, enabled);
    if (meter.liveJunction(reading) && enabled && panel_ == Panel::Meter) {
        meterEditor_.mcuJunction(reading);
        view_.properties(meterEditor_, navigator_);
    }
}

[[noreturn]] void ApplicationController::run() {
    prefs_ = preferences_.values();
    applyPreferences();
    // The factory record's reading is the settings' choice at boot only (Reload settings keeps it).
    const bool backend = data_.initialize(state_.mode, FactoryRecordPolicy(prefs_.scope.factoryRecordFormat));
    const auto applied = applyDataPreferences();
    restoreStartupOutput(applied); // After the saved settings were applied; never with refused ones.
    BootTiming::mark(BootTiming::Data);
    InstrumentBackend tmcBackend(*this), serialBackend(*this);
    scpi::Engine tmcEngine(tmcBackend), serialEngine(serialBackend);
    InstrumentUsb usb(tmcEngine,serialEngine); usb_ = &usb;
#ifdef OPENOSC_DIAG_MAILBOX
    // Diagnostic builds: the SWD mailbox, served through a backend of its own (local_control.hpp).
    InstrumentBackend localBackend(*this);
    diag::LocalControl local(localBackend);
#endif
#ifdef OPENOSC_INTERLEAVE_PROBE
    // The ZT703S interleave probe (interleave_probe_control.hpp): its SWD mailbox, DataAccess's probe
    // mode and a backend of its own for the held record's lease.
    struct ProbeData final : diag::ProbeTarget {
        DataAccess &data;
        explicit ProbeData(DataAccess &d) : data(d) {}
        bool setInterleaveProbe(scope::ProbeMode mode) override { return data.setInterleaveProbe(mode); }
        const scope::InterleaveProbe &interleaveProbe() const override { return data.interleaveProbe(); }
        uint32_t interleaveProbeBlockers() const override { return data.interleaveProbeBlockers(); }
    } probeData(data_);
    InstrumentBackend probeBackend(*this);
    diag::InterleaveProbeControl probe(probeData, probeBackend, openosc_interleave_probe);
#endif
    uiReady_ = display_.initialize();
    BootTiming::mark(BootTiming::Display);
    // Settings > Files & USB > USB controller, before the first clock change (headless Remote). An
    // explicit Disabled holds also without a display; Recovery restores the setting independently.
    usb.enable(prefs_.usbController != 0, Board::milliseconds());
    Watchdog::progress();
    // While a refresh waits on DMA2D: the watchdog, queued USB transfers and the acquisition
    // streams go on (no LVGL, FAT or UI state there; see RenderIdle).
    RenderIdle::install([](void *context, uint32_t now) {
        Board::service();
        CooperativeService::poll(now);
        auto &self = *static_cast<ApplicationController *>(context);
        self.data_.serviceBackground(now);
        self.prepareColumns();
    }, this);
    view_.waveform().columnCache(&plot::applicationColumns());
    boot_diagnostics.stage = 10;
    if (uiReady_) {
        if (!prefs_.useBuiltinTheme) themes_.reload();
        Watchdog::progress();
        buildView(); view_.measurement(state_);
        applyBrightness();
        char result[112]; applied.text(result,sizeof result);
        if (bootStart_.notice) view_.notify(Tooltip::Critical, bootStart_.notice, Board::milliseconds());
        else if (!applied.ok()) view_.notify(Tooltip::Critical, result,Board::milliseconds());
        else if (preferences_.error()) view_.notify(Tooltip::Critical, preferences_.error(), Board::milliseconds());
        else if (preferences_.repaired() && preferences_.restored()) view_.notify(Tooltip::Critical, "Bad settings archived; backup copy restored", Board::milliseconds());
        else if (preferences_.repaired()) view_.notify(Tooltip::Critical, "Bad settings archived; defaults written", Board::milliseconds());
        else if (preferences_.restored()) view_.notify(Tooltip::Critical, "Settings restored from a backup copy", Board::milliseconds());
        else if (const char *theme = ThemeNotice::text(themes_)) view_.notify(Tooltip::Critical, theme, Board::milliseconds());
        else if (themes_.error()) view_.notify(Tooltip::Critical, themes_.error(), Board::milliseconds());
        else if (data_.zeroFile().error()) view_.notify(Tooltip::Critical, data_.zeroFile().error(),Board::milliseconds());
        else if (const unsigned archived = data_.calibrationArchived()) {
            // Invalid, or bound to another factory record (its identity changed with the record's
            // format): kept as *.bad.NNNN.json, never rebound; those corrections are off now.
            char text[96];
            std::snprintf(text, sizeof text, "Calibration JSON archived (*.bad):%s%s%s%s",
                          archived & DataAccess::ZeroJson ? " zero" : "", archived & DataAccess::PwmJson ? " PWM" : "",
                          archived & DataAccess::GainJson ? " gain" : "", archived & DataAccess::InterleaveJson ? " interleave" : "");
            view_.notify(Tooltip::Critical, text, Board::milliseconds());
        }
        else if (boot_diagnostics.error==4) view_.notify(Tooltip::Critical, "High unavailable; Normal active",Board::milliseconds());
        BootTiming::mark(BootTiming::View);
    } else { // Headless: no UI; Remote (and its 200 MHz) only if that profile really runs.
        boot_diagnostics.error = 2; display_.enabled(false); setRemote(true);
    }
    // While a host enumerates the capture keeps quiet (usb::QuietAttach); DataAccess answers for it.
    struct CaptureQuiet final : usb::QuietClient {
        DataAccess &data;
        explicit CaptureQuiet(DataAccess &d) : data(d) {}
        bool quietCapture(bool quiet) override { return data.quietCapture(quiet); }
    } captureQuiet(data_);
    usb.quietWith(&captureQuiet); // Before any attach, the boot start's first.
    usb.followClock(ClockControl::activeMode()); // The boot profile: a high one leaves USB data off.
    if (usb.allowed() && !usb.start()) boot_diagnostics.error = 3; // Accepted and waiting counts as started.
    boot_diagnostics.stage = 11;
    // A headless start or one whose backend did not come up never confirms the 360 MHz profile.
    started_ = uiReady_ && backend && applied.ok();
    if (bootStart_.armed) guardConfirmation_.armed(Board::milliseconds());
    performance_.start(DWT->CYCCNT);
    InputService::enable();
    while (true) {
        const uint32_t passStart = Board::milliseconds();
        performance_.tick(DWT->CYCCNT);
        Watchdog::heartbeat(); // The one proof that the application's loop goes on (docs/WATCHDOG.md).
        StackGuard::check();   // A stack that reached .bss resets (recorded) instead of running on (issue #8).
        Board::service();
        const uint32_t now = Board::milliseconds();
        serviceMcuTemperature(now);
        data_.poll(now);
        serviceShutdown(now);
        serviceBootGuard(now);
        usb.poll(now);
#ifdef OPENOSC_DIAG_MAILBOX
        local.poll(now);
#endif
#ifdef OPENOSC_INTERLEAVE_PROBE
        probe.poll();
#endif
        serviceRecovery(now);
        if (uiReady_) {
            view_.usbConnected(usb.connected());
            view_.uptime(data_.nowUs());
        }
        serviceAuto(now);
        serviceInstruments(now);
        serviceZeroUi(now);
        serviceInterleaveOffsetUi(now);
        serviceGainMatchUi(now);
#if OPENOSC_BOARD == 703
        serviceInterleaveMatchUi(now);
#endif
        serviceBode(now);
        pauseHiddenCapture(now, usb.enumerating(now));
        serviceImages(now);
        if (uiReady_ && !shots_.busy()) {
            const uint32_t start = DWT->CYCCNT;
            displayData(now);
            performance_.ui(DWT->CYCCNT-start);
        }
        const bool activity=InputService::activity();
        const bool wake=servicePower(InputService::keys() || activity,now);
        if (wake) {
            InputService::wake();
            autoKey_.cancel();
        }
        if (InputService::overflowed()) {
            InputService::suppress();
            autoKey_.cancel();
            data_.closeDmmRange(); // Its release may be among the lost events.
            if (uiReady_) view_.notify(Tooltip::Critical, "Input overflow; press again",now);
        }
        { // The REL/Power hold, from the input sampler's own timing; the long press covers headless.
            uint32_t held = 0;
            const bool down = InputService::powerHold(held), holding = shutdown_ == Shutdown::None && down;
            if (!down) powerFired_ = false; // Up (debounced): the long press's end is no new hold.
            else if (holding && held >= prefs_.holdMs() && !recoveryPending_) powerOff(now);
            const int power = PowerHold::percent(holding, held, prefs_.holdMs());
            const int automatic = scopeAutoCountdown(now, power >= 0); // Every pass: its input state must not depend on the drawing.
            if (uiReady_) {
                if (power >= 0) view_.powerHold(power, "Hold to power off");
                else view_.powerHold(automatic, scopeAutoHint());
            }
        }
        TimedKeyEvents input;
        if (shots_.busy()) {
            shots_.service();
            boot_diagnostics.progress = shots_.percent();
            if (!shots_.busy()) finishScreenshot(now);
            else if (shots_.percent()/5 != shotProgress_/5) {
                shotProgress_=shots_.percent();
                char progress[40]; std::snprintf(progress,sizeof progress,"Saving screenshot: %u%%",shotProgress_);
                view_.notify(Tooltip::Progress, progress,now);
            }
            if (uiReady_) { view_.service(now); render(); }
        } else {
            if (InputService::take(input)) { onKeys(input.events,now); refreshSoftKeys(); } // Captions follow at once.
            if (prefs_.scope.decoder.enabled) serviceDecoder(now); // After input, USB and the instruments (plan §5.2).
            serviceDecoderCsv(now); // A file being saved: one bounded slice; nothing when none is.
            store(now);
            if (uiReady_) {
                const uint32_t start = DWT->CYCCNT;
                view_.service(now);
                if (!shots_.busy()) render();
                performance_.ui(DWT->CYCCNT-start);
            }
        }
        boot_diagnostics.action = uint32_t(state_.mode) | (state_.settingsOpen ? 0x100 : 0)
            | (state_.settingsOpen && settingsMenu_.information() ? 0x200 : 0);
        {
            const uint32_t end = Board::milliseconds();
            const bool menuPause = data_.capturePaused() && uint32_t(now - hiddenAt_) < OverlaySettleMs; // Paused by design.
            const bool expecting = state_.mode == MeasurementMode::Oscilloscope && !data_.scopeHeld() && !autoBusy() &&
                                   data_.scopeRequest().trigger.mode == scope::TriggerMode::Auto && !menuPause;
            const uint8_t held = (data_.captureQuieted() ? StallLog::Quiet : 0) | (data_.capturePaused() ? StallLog::Paused : 0);
            const auto &m = data_.scopeMetadata();
            const uint32_t recordMs = m.samplesPerSecond.valid() ? uint32_t(std::min(60000.0, m.samples * 1000.0 / m.samplesPerSecond.value())) : 0;
            stalls_.frame(data_.scopeFrameSequence(), expecting, held, end, recordMs);
            stalls_.pass(passStart, end);
        }
        // Keep the foreground polling loop awake while the PIO USB core is active.
        // WFI caused reproducible EP0 stalls/re-enumeration on this ZT703S.
    }
}
// One refresh; its waits on DMA2D reduce the next frame ahead (prepareColumns). The look at that
// frame ends with the refresh: no lease stays held between loops (clock changes, SCPI).
void ApplicationController::render() {
    rendering_ = true;
    display_.render();
    BootTiming::mark(BootTiming::Frame);
    rendering_ = false;
    ahead_.release();
}
// One slice of the record being decoded (plan §5.2): the budget is checked after each 128-point
// block, so a slice ends at most one block past it. DWT counts core cycles at any clock profile.
void ApplicationController::serviceDecoder(uint32_t now) {
    static constexpr uint32_t SliceUs = 300;
    syncHostRecord(now);
    if (!decoder_.running()) return;
    decoder_.step({[]() -> uint32_t { return DWT->CYCCNT; }, SystemCoreClock / 1000000U * SliceUs, now});
}
void ApplicationController::applyPreferences() {
    state_.mode = static_cast<MeasurementMode>(prefs_.mode);
    state_.scope.timebaseNs = prefs_.timebaseNs;
    state_.scope.measurements = prefs_.scope.measurements;
    for (unsigned c = 0; c < 2; ++c) state_.scope.millivoltsPerDivision[c] = prefs_.millivolts[c];
    state_.meter.function = prefs_.meterFunction;
    configureDecoder();
    if (usb_) usb_->enable(prefs_.usbController != 0, Board::milliseconds()); // Reload settings JSON.
    // Only the hold mode and its parameters: a cycle or snapshot never comes back by itself.
    if (data_.meter().holdCycle().settings() != prefs_.meter.hold) data_.configureMeterHold(prefs_.meter.hold);
    idle_.configure(prefs_.idle,Board::milliseconds());
    battery_.trim(prefs_.batteryGainPermille, prefs_.batteryOffsetMv);
    view_.tooltips(prefs_.tooltipsHidden); // Boot and Reload settings JSON (GitHub #108).
    view_.menuContrast(prefs_.menuContrast); // GitHub #44: the menus drawn from now on.
    view_.channelView(plot::ChannelView{prefs_.swapChannels != 0}); // Swap CH1/CH2: the Scope view only.
    dimmed_=false;
    applyBrightness();
    dataRefresh_=true;
}
void ApplicationController::powerOff(uint32_t now) {
    if (powerFired_) return; // Once a hold: a refusal is not repeated every pass.
    powerFired_ = true;
    if (panel_==Panel::Zero) data_.cancelZero(now);
    if (panel_ == Panel::GainMatch) finishGainMatch(now, Tooltip::Done, nullptr, false);
#if OPENOSC_BOARD == 703
    if (panel_ == Panel::InterleaveMatch) finishInterleaveMatch(now, Tooltip::Done, nullptr, false);
#endif
    if (dropEdit(now) && uiReady_) showSettings(); // Not saved, not kept if the power off is refused.
    confirmAction_=ShutdownAction::PowerOff; requestShutdown(now);
}
void ApplicationController::onKeys(const KeyEvents& events, uint32_t now) {
    const uint32_t keys = events.shortPress | events.repeat;
    navigator_.repeats(events.repeat & ~events.shortPress);
    navigator_.leftStep(prefs_.menuLeftStep);
    if (shutdown_ != Shutdown::None || recoveryPending_) return;
    meterRangeKeys(events, now); // KEY5 in the DMM: its own press, release and hold events.
    scopeAutoKeys(events, now);  // KEY5 on the Scope waveform screen: the same events, its own actions.
    if (!(events.shortPress | events.longPress | events.repeat)) return;
    // REL/Power held to the end of its progress (PowerHold): the power-off pipeline at once, from
    // every screen, menu, dialog, Remote or headless; it saves settings and files and stops the
    // instruments, and says why when it cannot.
    if (events.longPress & Keypad::Relative) { powerOff(now); return; }
    if (panel_==Panel::Zero) { zeroKeys(keys,now); return; }
    if (panel_ == Panel::GainMatch) { gainMatchKeys(keys, now); return; }
#if OPENOSC_BOARD == 703
    if (panel_ == Panel::InterleaveMatch) { interleaveMatchKeys(keys, now); return; }
#endif
    if (panel_ == Panel::Bode) { bodeKeys(events, keys, now); return; } // MODE leaves it (restored); Remote ends it in serviceBode.
    if (bodeRestoring()) { // Closing Bode can leave its generator ACK pending; the restore still owns Scope.
        if ((events.longPress & Keypad::HoldSave) && !remote_ && uiReady_) startScreenshot(now); // Only the image (#76).
        return;
    }
    if (remote_ || !uiReady_) { // Locked: Remote, or no UI to operate (headless).
        if (events.longPress & Keypad::Menu) setRemote(false, "USB Remote off");
        return;
    }
    if (panel_ == Panel::Confirmation) { confirmationKeys(keys,now); return; }
    if (panel_ == Panel::Images) {
        if (events.longPress&Keypad::Menu) { closeImages(); settingsMenu_.open(); navigator_.reset(); showSettings(); }
        else if ((events.longPress&Keypad::HoldSave) && !imageReader_.busy() && !images_.clearing()) {
            startScreenshot(now);
        } else imageKeys(keys,now);
        return;
    }
    if ((events.shortPress | events.longPress) & Keypad::Mode) { // A tap: the next of three screens; held: heldMode.
        const auto target = events.shortPress & Keypad::Mode ? nextMode(state_.mode) : heldMode(state_.mode);
        if (selectMode(target, now) == ModeChange::Refused) view_.notify(Tooltip::Critical, "Cannot switch measurement hardware",now);
        return;
    }
    if ((events.longPress & Keypad::Menu) && !state_.settingsOpen) {
        if (!cancelAuto(now)) return;
        panel_ = Panel::None;
        state_.settingsOpen = true;
        settingsMenu_.open();
        navigator_.reset();
        showSettings();
    } else if (events.longPress & Keypad::HoldSave) {
        startScreenshot(now);
    } else if (scopeHoldKeys(events.longPress, now)) {
        // F1-F4 held on the Scope waveform screen; a hold never also gives its tap (KeyGestures).
    } else if (state_.settingsOpen) settingsKeys(keys, now);
    else panelKeys(keys, now, !(events.repeat & (Keypad::Up|Keypad::Down|Keypad::Left|Keypad::Right)));
}
void ApplicationController::changed(uint32_t) { saves_.settingsChanged(); }
// The guarded profile confirmed after its stable time (BootConfirmation); no NOR write while a host is
// active (STORE-02) or a shutdown or the Recovery handoff runs.
void ApplicationController::serviceBootGuard(uint32_t now) {
    // Also not while HOLD waits for its record (scopeHoldPending): the confirmation is not consumed, the next pass writes it.
    if (!guard_ || shutdownSequence_.active() || recoveryPending_ || (usb_ && usb_->hostActive(now)) || data_.scopeHoldPending()) return;
    if (!guardConfirmation_.due(now, ClockControl::activeMode() == OverclockedRunMode, started_)) return;
    stalls_.mark(StallLog::Guard);
    guardConfirmation_.written(guard_->confirm(), now);
    Watchdog::progress();
}
// One attempt, never a reason to stay on (Q-01): a failed write leaves "starting", the conservative side.
void ApplicationController::stopBootGuard() {
    if (!guard_ || !guardConfirmation_.waiting()) return;
    guardStopped_ = guard_->stop();
    Watchdog::progress();
    guardConfirmation_.cancel();
}

// Each save, failed or not, is a finished step (WatchdogPolicy): two saves in one pass may each end
// only after a NOR timeout (5 s), longer together than the watchdog allows without progress.
bool ApplicationController::SettingsStore::saveTheme() {
    a_.stalls_.mark(StallLog::Theme);
    auto options = a_.view_.waveform().options();
    // HOLD inspection belongs to the retained record. Persist the view that RUN will restore.
    if (a_.heldViewActive_) options.restoreHeldView(a_.heldView_);
    const bool saved = a_.themes_.savePlot(options);
    Watchdog::progress();
    return saved;
}
bool ApplicationController::SettingsStore::savePreferences() {
    usbtrace::mark(usbtrace::Tag::SaveBegin);
    a_.stalls_.mark(StallLog::Save);
    const bool saved = a_.preferences_.save(a_.prefs_);
    usbtrace::mark(usbtrace::Tag::SaveEnd);
    Watchdog::progress();
    return saved;
}
// Settings and theme are saved only by ShutdownSequence (PendingSaves).
void ApplicationController::store(uint32_t now) {
    // The block cache's pulse (a NOR erase, a stall) only while nobody uses the device, and never once a
    // shutdown or the Recovery handoff runs: after its unmount nothing may touch the volume again.
    const bool unused = !shutdownSequence_.active() && !recoveryPending_ && idle_.quietMs(now) >= UnusedMs && !(usb_ && usb_->hostActive(now)) && !autoBusy() && !data_.zeroBusy() &&
                        !shots_.busy() && !imageReader_.busy() && !data_.scopeHoldPending() && // HOLD waits for its record: no NOR transfer.
                        !(state_.mode == MeasurementMode::Oscilloscope && data_.scopeRequest().trigger.mode == scope::TriggerMode::Single && !data_.scopeHeld());
    if (fs_.service(now, unused)) stalls_.mark(StallLog::Save); // A cache flush happened: a NOR erase in this pass.
    followGeneratorOutput(now); // A confirmed OUTP ON just before a save is in it.
    // The power-off save's volume while changes wait (UI only): every minute, at once when they start or Settings opens.
    const bool pending = saves_.settingsPending() && uiReady_ && !shutdownSequence_.active(); // The sequence reports its own save.
    const char *before = saveProblem_;
    const bool opened = state_.settingsOpen && !saveSettingsOpen_;
    saveSettingsOpen_ = state_.settingsOpen;
    if (!pending) { saveProblem_ = saveWarned_ = nullptr; saveCheckedAt_ = now - 60000; saveWarnedAt_ = now - 600000; }
    else if (opened || int32_t(now - saveCheckedAt_) >= 60000) {
        saveCheckedAt_ = now;
        saveProblem_ = !fs_.mounted() ? "No storage: settings will not save" : !fs_.refreshFree() ? "Storage error: settings may not save"
                     : fs_.freeBytes() < 16384 ? "Storage full: settings will not save" : nullptr;
    }
    if (uiReady_ && state_.settingsOpen && !settingsMenu_.information() && (saveProblem_ != before || pending != saveShownPending_))
        showSettings();
    // The same problem every 10 minutes, another one at least 2 after the last notice; never over another notice.
    if (!saveProblem_) saveWarned_ = nullptr;
    if (int32_t(now - saveWarnedAt_) > 600000) saveWarnedAt_ = now - 600000; // No int32 wrap on long uptimes.
    const int32_t since = int32_t(now - saveWarnedAt_);
    if (saveProblem_ && !view_.toastShowing() && (since >= 600000 || (saveProblem_ != saveWarned_ && since >= 120000))) {
        saveWarned_ = saveProblem_; saveWarnedAt_ = now; view_.notify(Tooltip::Critical, saveProblem_, now);
    }
}
void ApplicationController::queuePlotSave(uint32_t now) {
    saves_.plotChanged();
    prefs_.useBuiltinTheme = 0;
    changed(now);
}
void ApplicationController::showSettings() {
    if (settingsMenu_.information()) {
        view_.information(settingsMenu_, settingsInfo_);
        return;
    }
    char saved[48];
    std::snprintf(saved, sizeof saved, "Saved: %s", storage::Layout::SettingsFile);
    saveShownPending_ = saves_.settingsPending();
    const char* status = preferences_.error() ? preferences_.error() : !saves_.settingsPending() ? saved
                       : saveProblem_ ? saveProblem_ : "Saved at power-off";
    view_.settings(settingsMenu_, settingsRows(), navigator_, status);
}
}
