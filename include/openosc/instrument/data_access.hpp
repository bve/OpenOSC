#pragma once
#include "openosc/instrument/frame_hosts.hpp"
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/trigger_level.hpp"
#include "openosc/acquisition/sample_plan.hpp"
#include "openosc/acquisition/interleave.hpp"
#include "openosc/board/current.hpp"
#include "openosc/calibration/interleave_measurement.hpp"
#ifdef OPENOSC_INTERLEAVE_PROBE
#include "openosc/acquisition/interleave_probe.hpp"
#endif
#include "openosc/acquisition/capture.hpp"
#include "openosc/acquisition/store.hpp"
#include "openosc/acquisition/background_reader.hpp"
#include "openosc/acquisition/request.hpp"
#include "openosc/acquisition/rolling.hpp"
#include "openosc/acquisition/trend.hpp"
#include "openosc/acquisition/arena.hpp"
#include "openosc/acquisition/pulse_counters.hpp"
#include "openosc/board/drivers.hpp"
#include "openosc/data/recorders.hpp"
#include "openosc/instrument/mode.hpp"
#include "openosc/instrument/clock_suspend.hpp"
#include "openosc/measurement/dmm/packet.hpp"
#include "openosc/measurement/dmm/uart_dma.hpp"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/link_watchdog.hpp"
#include "openosc/measurement/dmm/meter_commands.hpp"
#include "openosc/measurement/dmm/ranges.hpp"
#include "openosc/measurement/dmm/reading_trace.hpp"
#include "openosc/calibration/zero_operation.hpp"
#include "openosc/calibration/zero_file.hpp"
#include "openosc/calibration/zero_selection.hpp"
#include "openosc/calibration/zero_calibration.hpp"
#include "openosc/calibration/pwm_file.hpp"
#include "openosc/calibration/pwm_zero_calibration.hpp"
#include "openosc/calibration/gain_calibration.hpp"
#include "openosc/calibration/gain_file.hpp"
#include "openosc/calibration/interleave_file.hpp"
#if OPENOSC_BOARD == 703
#include "openosc/calibration/interleave_match_file.hpp"
#endif
#include "openosc/calibration/scope_calibration.hpp"
#include "openosc/storage/storage_admission.hpp"

namespace openosc {
class DataAccess : private calibration::ZeroInstrument, private generator::BusClient, private StorageAdmission::Capture {
  public:
    explicit DataAccess(Filesystem &fs);
    ~DataAccess() { StorageAdmission::detach(this); }
    DataAccess(const DataAccess &) = delete;
    DataAccess &operator=(const DataAccess &) = delete;
    // factoryRecord: how the factory calibration record is read (the settings' choice, read at boot
    // only: SYST:CAL:FACT:FORM? reports this one, not a later Reload settings).
    bool initialize(MeasurementMode view, FactoryRecordPolicy factoryRecord = FactoryRecordPolicy::Auto);
    FactoryRecordPolicy factoryRecordPolicy() const { return factoryRecordPolicy_; }
    // Calibration files set aside (archived *.bad) by their last load, at boot or Reload: invalid or
    // bound to another factory record (an identity that changed with the record's format).
    enum CalibrationFiles : unsigned { ZeroJson = 1, PwmJson = 2, GainJson = 4, InterleaveJson = 8 };
    unsigned calibrationArchived() const;
    bool select(MeasurementMode view);
    void noteFrameDemand(uint32_t now) { frameRequestedAt_ = now; capturePaused_ = false; }
    // Skips capture restarts while nothing consumes frames; a recent frame request overrides it.
    bool fullRateCapture() const { return configuration_.samplesPerSecond.value() >= FullRateSamples; }
    void pauseCapture(bool pause,uint32_t now) { capturePaused_ = pause && uint32_t(now-frameRequestedAt_)>=FrameDemandMs; }
    bool capturePaused() const { return capturePaused_; }
    bool captureQuieted() const { return usbQuiet_; }
    bool configureScope(const ScopeRequest &request);
    // While held: keeps the record and applies the configuration at the next RUN.
    bool stageScope(const ScopeRequest &request);
    // Leaves HOLD and applies the request as one step: a refusal keeps the record, HOLD and settings.
    bool resumeScope(const ScopeRequest &request);
    bool scopeSingleDone() const { return singleDone_; }
    uint32_t scopeHoldReleases() const { return holdReleases_; }
    // AutoSetup: retain() keeps the frame on show (HOLD or the last RUN frame) where it lies while
    // trials() capture into the separate trial area; restoreHeldScope() puts it back in HOLD
    // with its settings, releaseRetained() gives its memory back when it is no longer wanted.
    bool retainScope();
    void scopeTrials(bool trials) { trials_ = trials; }
    void releaseRetained();
    bool restoreHeldScope(const ScopeRequest &request);
    scope::AcquisitionArena &scopeArena() { return arena_; }
    const scope::AcquisitionArena &scopeArena() const { return arena_; }
    const ScopeRequest &scopeRequest() const { return request_; }
    // The acquisition the current request plans (per-channel points, rate, layout); a frame's own
    // metadata says what it actually holds. In HOLD this is the plan of the next RUN.
    const scope::FrameMetadata &scopeMetadata() const { return configuration_; }
    // What a channel's samples would mean under `request`, without applying anything.
    scope::ChannelMetadata scopeChannelFor(const ScopeRequest &request, unsigned channel) const;
    // consume=false: a look that leaves the frame unseen (it is not released for capture).
    bool acquireScope(scope::FrameLease &lease, bool consume = true);
    // The record on show (`shown`: its metadata as the view has it), while its memory still holds it,
    // annotated with the trigger it was shown with: the view moves it again when acquireScope gives
    // nothing (Normal or Single waiting, issue #89). No other reader takes a record this way.
    bool acquireShownScope(scope::FrameLease &lease, const scope::FrameMetadata &shown);
    // A record in the store (the one on show, or a newer one the view takes next): with none, a
    // waiting capture took the shown record's memory and the view cannot be projected again.
    bool scopeRecordStored() const { return store_.hasFrame(); }
    // The record on display, leased for readouts computed beside the waveform (ReadoutScheduler).
    // DataAccess revokes it itself before anything that needs that memory or a new capture
    // (configuration, HOLD/RUN, retain, zero, measurement change, shutdown), so nobody has to wait
    // for a background task; each revocation advances readoutRevision().
    const scope::FrameView *readoutFrame(uint32_t sequence);
    void releaseReadout();
    uint32_t readoutRevision() const { return readoutRevision_; }
    // The protocol decoder's job keeps its own lease of the record on show (RUN Complete, HOLD): it is
    // revoked with the readout's, before anything that needs that memory or a new capture.
    void attachBackground(scope::BackgroundReader *reader) { background_ = reader; }
    uint32_t scopeReaders() const { return store_.readers(); }
    // For one-shot actions: the newest record may still be copying when a key arrives.
    bool acquireScopeWait(scope::FrameLease &lease, uint32_t timeoutMs = 120);
    void holdScope(bool hold);
    // RUN (SCPI RUN, the HOLD key, leaving X-Y): leaves HOLD. Settings staged in HOLD (stageScope) go
    // through the transactional resumeScope: the old plan's record and capture are withdrawn and the
    // first record is the staged plan; a refusal (a reader still holds the record, the capture refuses)
    // keeps HOLD, its record and the staged settings. Without staging the acquisition goes on as it was.
    bool runScope();
    // TFOR: a new record of the current capture without waiting for an edge, cut at the force point
    // with its pre- and post-trigger parts filled (FrameMetadata::forced; `triggered` only when an
    // edge the settings ask for is in it). Single completes on it, Normal shows it, Auto too; the
    // trigger settings stay. A capture still filling its pre-trigger part, waiting for memory or
    // restarting after a fault takes the force when it can (bounded by that capture). True when a
    // record is on the way (Roll runs free and needs none); false: not running (not Scope, HOLD,
    // Single done, zero, AutoSetup, shutdown).
    bool forceScope();
    bool scopeHeld() const { return scopeHeld_; }
    // HOLD waits for the running capture's record (one slot, at most HoldWaitMs): deferrable writes
    // (background saves, the boot guard's confirmation) wait too, so their NOR transfers do not stop
    // that capture and leave HOLD empty.
    bool scopeHoldPending() const { return holdPending_; }
    // usb::QuietClient's call (usb_quiet.cpp, through the controller's adapter): while a host
    // enumerates, a fast capture stops and fast ones do not start; true once that holds. Apart from
    // the overlay pause (pauseCapture): frame demand never ends it.
    bool quietCapture(bool quiet);
    // TRIG:STAT? WAIT and the trigger indicator (Normal, Single): from the arm until a record is
    // accepted, and again once the running capture has waited TriggerWaitMs for an edge with its gate
    // Armed (followTriggerWait). Auto: never. HOLD and a finished Single read STOP (scopeHeld).
    bool scopeWaiting() const { return triggerWaiting_; }
    bool scopeTriggered() const { return acceptedTriggered_; } // The last accepted frame held its trigger edge.
    // Records of a real trigger since start (GitHub #169, Wake on measurement): a serial that steps
    // once for each fresh live record serviceScopeFrames accepts with its trigger edge (Auto, Normal
    // or a Single that triggered), never for an Auto timeout, a forced record, Roll, an internal
    // capture (AutoSetup trials, zero calibration, the interleave offset measurement) or a record
    // from before the latest arm, entry, setting change, HOLD or RUN (triggerFloor_). Modular: the
    // reader keeps its last value and compares. Apart from scopeTriggered (TRIG:STAT?), unchanged.
    uint32_t scopeTriggerEvents() const { return triggerEvents_; }
    // A physical F1-F4: one group key (dmm::MeterCommands::pressGroup), the statistics restart; what the
    // controller does is read from its packets. SCPI and the restore select a function instead.
    bool sendDmmGroup(dmm::CommandGroup group);
    // In the DMM, and in the Scope while its DMM trace runs (meterTraceRunning).
    bool selectDmmFunction(dmm::Function function);
    // The Scope's DMM trace, asked for by the view: while the Scope is the measurement the DMM chip is
    // powered beside the capture by bit 13 of the scope word (AnalogFrontend::dmmPower), its UART
    // receives by interrupt (the capture holds DMA1 Stream0), and its packets go into the DMM's session
    // (meter()) under LinkWatchdog, which power-cycles it by that bit alone. Leaving the Scope, a
    // shutdown or the trace off unpower the chip (LinkWatchdog::left: the DMM then keeps it off for
    // OffMs as after a DMM visit); a clock change restarts only its UART.
    void meterTrace(bool asked) { meterTraceAsked_ = asked; }
    bool meterTraceRunning() const { return meterTraceRunning_; }
    // Counts each start of the chip under the Scope: it comes up in its default function, so the
    // caller selects the saved one (as on a DMM entry). Restarts after silence: dmmRestarts().
    uint32_t meterTraceStarts() const { return meterTraceStarts_; }
    // The readings the trace draws: time on the frames' clock (nowUs), the value in SI, a slow
    // timebase's averaged into its entries (ReadingTrace::push). Kept through every capture, timebase
    // and measurement (a HOLD freezes them; a stop, a power cycle or a UART restart ends the line at
    // once, a gap); restarted when another function is selected in the Scope (under HOLD, at the first
    // reading after it) or a reading's quantity changes.
    const dmm::ReadingTrace &meterReadings() const { return meterReadings_; }
    // The trend's copy on show (ScopeRequest::trend): valid while its sequence is not 0.
    const scope::TrendView &trendShown() const { return trend_.shown(); }
    // AUTO/RANGE (pressDmmRange, setDmmAuto, setDmmRange, stepDmmRange) only where the function shown
    // has ranges (dmm::Ranges::selectable): elsewhere refused at once, nothing sent, nothing busy.
    bool dmmRanged() const { return !meter_.typeK() && dmm::Ranges::selectable(dmm::Functions::identify(meter_.latest())); }
    // The physical AUTO/RANGE key as the factory's KEY5 (dmm::RangeStroke, one held state): its
    // qualified press sends 20 20 and opens the stroke (a NextRange command, refused while busy);
    // releaseDmmRange(true), let go within the factory's 500 ms window, ends it with 21 21; (false), let go
    // later, ends it with nothing; each holdDmmRange (2 s, then every 2 s held) sends 22 22, the first
    // making the command Auto. Without an open stroke (its press refused or taken by a wake) release and
    // hold do nothing. A press needs a fresh reading of this DMM session (MeterCommands::FreshUs).
    // closeDmmRange: the context took the key (a panel, Remote, an input overflow): within the window
    // its 21, later nothing; its command cancelled, never confirmed.
    bool pressDmmRange();
    bool releaseDmmRange(bool sendRelease);
    bool holdDmmRange();
    bool dmmRangeOpen() const { return dmm_.rangeOpen(); }
    void closeDmmRange();
    // Auto: a synthetic hold (20, 22 HoldGapMs later); Manual: a synthetic short stroke (20, 21). It and
    // setDmmRange in the DMM, and in the Scope while its DMM trace runs (meterTraceRunning).
    bool setDmmAuto(bool automatic);
    // The manual HOLD (and the DMM's own, released by its key); off also ends a mode's cycle.
    bool holdMeter(bool held);
    // Delayed/Stable/Auto (dmm::HoldDetector): the cycle armed now (false outside the DMM, in Manual,
    // under the manual HOLD), cancelled; settings applied (cancels a cycle, never arms one). Leaving
    // the DMM cancels it.
    bool armMeterHold();
    void cancelMeterHold() { meter_.cancelHold(); }
    void configureMeterHold(const dmm::HoldSettings &settings) { meter_.configureHold(settings); }
    bool relativeMeter(bool enabled);
    bool setDmmRange(unsigned index);
    bool stepDmmRange(int direction);
    bool serialMeter(bool enabled);
    const board::DmmSerialOutput &meterSerial() const { return serial_; }
    bool dmmBusy() const { return dmm_.busy(); }
    // DMM chip restarts that brought it back (dmm::LinkWatchdog); its function is then lost.
    uint32_t dmmRestarts() const { return watchdog_.restarts(); }
    uint32_t dmmReselects() const { return watchdog_.reselects(); } // Powered again in its default function, silently.
    DataStatus dmmCommandStatus() const { return dmm_.status(); }
    const char *dmmCommandName() const { return dmm_.operation(); }
    DataStatus dmmSelectionStatus() const { return dmm_.selectionStatus(); }
    bool recordScope(ScopeRecorder::Format format);
    ScopeRecorder &scopeRecorder() { return recorder_; }
    bool scopeRecording() const { return recordPending_ || recorder_.busy(); }
    DataStatus scopeRecordStatus() const { return recordStatus_; }
    DmmCsvLogger &meterLogger() { return logger_; }
    // DMM session logging (GitHub #144): starts on the DMM's current reading (its function, the
    // DMM's own REL) at nowUs(); every later decoded packet goes to it, as to the per-packet log. It
    // ends on stop, its duration, leaving the DMM (activate) and at shutdown (beginShutdown), each
    // committed by poll(). False: not in the DMM, no reading, or the recorder's status() says why.
    bool startMeterSession(const dmm::LogSettings &settings);
    void stopMeterSession() { session_.stop(timeUs_, dmm::LogEnd::Manual); }
    const DmmSessionRecorder &meterSession() const { return session_; }
    DmmSessionRecorder &meterSession() { return session_; }
    uint32_t scopeCaptures() const { return capture_.completed(); }
    // The newest frame a reader is given (acquireScope's rule): finite records and Roll snapshots
    // alike, once serviceScopeFrames accepted them (Auto, an edge, forced). Takes no lease.
    uint32_t scopeFrameSequence() const { return acceptedSequence_; }
    // The USB sessions' claims on the accepted record on show (FrameHosts): their backends report
    // demand and what they leased; reserveCapture keeps an owed record's memory.
    FrameHosts &frameHosts() { return frameHosts_; }
    scope::CaptureProgress scopeProgress() const { return capture_.progress(); }
    scope::CounterSnapshot scopeCounters() const;
    // Analyze > Display > Frequency counter, CH1 below 5 kHz (GitHub #173): false (Gate) keeps TIM2
    // on its ETR count with no interrupt; true times a slow CH1 by its edges (PulseCounters). Cheap
    // to repeat. A counter stop (calibration, clock change, another view) returns CH1 to its gate; the
    // choice stays. HOLD leaves the counters running.
    void timeScopeCounter(bool timed) { counters_.timed(timed); }
    // hardware: the offset PWM itself (scope_pwm.json), else the software zero (scope_zero.json).
    bool calibrateZero(uint32_t now,bool hardware=false);
    // The gain of one channel and range against `referenceVolts` at its probe tip (scope_gain.json).
    // Refused before anything changes when that reference cannot give a usable span (InvalidArgument,
    // see gainSpan) or as calibrateZero refuses. Then: proceedGain once the reference is on the tip,
    // acceptGain to save the result (cancelZero drops it).
    DataStatus calibrateGain(uint32_t now,unsigned channel,unsigned range,double referenceVolts);
    bool gainSpan(unsigned channel,unsigned range,double referenceVolts,double &codes) const;
    bool proceedGain(uint32_t now) { return gainRunning() && gain_.proceed(now); }
    bool acceptGain() { return gainRunning() && gain_.accept(); }
    bool gainRunning() const { return zeroBusy() && &zeroOperation_.calibration()==&gain_; }
    const calibration::GainCalibration &gainCalibration() const { return gain_; }
    calibration::GainFile &gainFile() { return gainFile_; }
    // The user gain off (its values stay in the file); refused when the file was not read.
    bool factoryGain();
    // The cell cannot be saved without changing others (GainProfile::cellSavable): the saved profile
    // is off (Use factory gain) and holds other factors that saving would bring back.
    bool gainCellBlocked(unsigned channel,unsigned range) const {
        return gainFile_.loaded() && !gainFile_.values().cellSavable(channel,range);
    }
    // ChannelMatch (GitHub #80) on the user's own RUN records: Ok, or Busy (not Scope RUN, HOLD, a
    // calibration, Auto trials or a recording), Uncalibrated (no factory record, a channel without
    // scale), IoError (scope_gain.json unread), InvalidArgument (not both channels on at one V/div,
    // DC, or Roll). Both channels on: no interleave and no range boost, the physical range is the V/div.
    DataStatus gainMatchReady() const;
    // One measured cell into scope_gain.json, then in force for new records: every other cell as
    // saved. InvalidArgument: a factor out of bounds or gainCellBlocked; IoError: not read or not
    // written (the gain in force stays).
    DataStatus saveGain(unsigned channel,unsigned range,double factor);
    // The factory scale gain of a channel (record word / 100, the built-in 1.20 without a record), 0: none.
    double factoryScaleGain(unsigned channel) const {
        return scopeCalibration_.nominal() ? calibration::ScopeCalibration::NominalGain
             : calibration_.factoryScale(channel) ? calibration_.displayGain(channel) : 0;
    }
    // ZT706S service interleave offset (Unsupported elsewhere, and while a calibration runs):
    // apply: in force for new records, not saved; save: the one in force to scope_interleave.json;
    // cancel: back to the saved one; reset: the factory record's, saved.
    DataStatus applyInterleave(float codes);
    DataStatus saveInterleave();
    DataStatus cancelInterleave();
    DataStatus resetInterleave();
    calibration::InterleaveFile &interleaveFile() { return interleaveFile_; }
    double gainInForce(unsigned channel,unsigned range) const { return scopeCalibration_.gain().factor(channel,range); }
    // The HIGH ADC's offset a new interleaved record takes, frozen in its metadata: the B2 service
    // profile (applied or saved) else the factory record's (ScopeCalibration::interleaveOffset); and
    // the saved one.
    float interleaveOffset() const { return scopeCalibration_.interleaveOffset(); }
    // No factory record: the Scope runs on the built-in defaults (ScopeCalibration::nominal), approximate.
    bool scopeNominal() const { return scopeCalibration_.nominal(); }
    const calibration::InterleaveProfile &savedInterleave() const { return interleaveFile_.values(); }
    // Interleave of CH1 (InterleaveRule): the ZT703S user's choice (Settings > Device > Performance >
    // Interleave CH1; off by default, any other board accepts only off), what keeps a request from it.
    bool selectInterleave(bool on) { return selectInterleave(on ? scope::InterleaveMode::On : scope::InterleaveMode::Off); }
    bool selectInterleave(scope::InterleaveMode mode);
    bool interleaveSelected() const;
    uint32_t interleaveBlockers(const ScopeRequest &request) const;
    uint32_t interleaveBlockers() const { return interleaveBlockers(request_); }
    // ZT703S interleave offset of the clock that runs (Scope calibration > Interleave offset): begin
    // (Ok, Busy, InvalidArgument: not interleaving now or not Auto, Unsupported), cancel, its state;
    // whether the clock that runs has one (else its records interleave uncalibrated, offset 0).
    DataStatus beginInterleaveOffset(uint32_t now);
    void cancelInterleaveOffset();
    const calibration::InterleaveOffsetMeasurement &interleaveMeasurement() const { return interleaveMeasure_; }
    bool interleaveCalibrated() const;
    const float *interleaveOffsetAtClock() const; // The ZT703S offset measured at the clock that runs, or null.
#if OPENOSC_BOARD == 703
    DataStatus interleaveMatchReady() const;
    void interleaveMatchCalibrating(bool on) { matchCalibrating_ = on; }
    DataStatus saveInterleaveMatch(uint32_t coreHz, const Rational &rate, const scope::ChannelMetadata &channel,
                                   const scope::InterleaveMatch &filter);
#endif
#ifdef OPENOSC_INTERLEAVE_PROBE
    // Diagnostic probe (interleave_probe.hpp, env firmware_interleave_probe): the mode for new captures,
    // applied through an ordinary forced reconfigure (refused while held, during a calibration, an
    // AutoSetup or a recording); what blocks it for the request now; the mode a record was captured in.
    bool setInterleaveProbe(scope::ProbeMode mode);
    uint32_t interleaveProbeBlockers() const;
    const scope::InterleaveProbe &interleaveProbe() const { return probe_; }
#endif
    void cancelZero(uint32_t now);
    bool zeroBusy() const { return zeroOperation_.busy(); }
    const calibration::ZeroOperation &zeroOperation() const { return zeroOperation_; }
    calibration::ZeroFile &zeroFile() { return zeroFile_; }
    calibration::PwmFile &pwmFile() { return pwmFile_; }
    const calibration::ScopeCalibration &scopeCalibration() const { return scopeCalibration_; }
    // Every calibration file again (hardware zero, software zero, gain, interleave); the new
    // values apply to new captures.
    bool reloadCalibration();
    // Both zeros off (hardware first, then software); their values stay in the files.
    calibration::ZeroSelection::Result factoryZero();
    dmm::Session &meter() { return meter_; }
    board::GeneratorOutput &generator() { return generator_; }
    // The ZT703S DAC stream shares the bus (Q-14, bus_load.cpp): while it runs (or announces its
    // start) the capture's maximum sample rate is halved (captureRateLimit). PWM and the ZT706S
    // module never load it. The one rule for the plans and for what the UI says about them.
    bool generatorLoadsBus() const;
    MeasurementMode measurementMode() const { return measurement_; }
    void poll(uint32_t nowMs);
    // What poll() does to the capture, generator, counter and UART streams, without FAT, UI or
    // LVGL: for the render idle hook while DMA2D draws (a frame's draw tasks hold their own
    // coordinates, so nothing they read changes).
    void serviceBackground(uint32_t nowMs);
    void beginShutdown();
    bool resume();
    // A core clock change (RunModeSwitch): every PLL1-timed user stops (capture TIM1/TIM3/TIM14 and
    // DMA, counters TIM2/TIM4/TIM5, generator TIM6/TIM13 and BDMA, DMM UART5, serial USART6), then
    // starts again from the new bus clocks. HOLD, a finished Single, the records, the requests and
    // the generator and serial settings stay; the Next plan (configuration_) follows the new clock.
    // Busy (nothing stopped) while a zero calibration, a record save, a DMM command or transfer, a
    // Roll snapshot copy, a retained record or a frame lease other than the HOLD one is in progress;
    // the background readout lease is given back, as for any change.
    ClockSuspend suspendClockUsers();
    // False when a user did not start again (its status says why); a capture waiting for the
    // memory of a frame still on show counts as started: poll starts it.
    bool resumeClockUsers();
    // The ZT706S module's OFF frame goes out first (bounded: ModuleLink::ExchangeMs). Ready also
    // when it was not acknowledged: power off, restart and Recovery do not hang on the module;
    // generatorOffConfirmed() says which, and the next boot sends OFF again.
    bool shutdownReady() const { return stopping_ && !dmmClosing_ && !recorder_.busy() && !logger_.active() && !session_.active() &&
                                         !generator_.busy(); }
    bool generatorOffConfirmed() const { return generator_.offConfirmed(); }
    DataStatus status() const { return status_; }
    uint64_t nowUs() const { return timeUs_; }

  private:
    DataStatus zeroAvailable() const override;
    const ScopeRequest &zeroRequest() const override { return request_; }
    uint16_t zeroFactoryCrc() const override { return calibration_.crc(); }
    bool zeroHeld() const override { return scopeHeld_; }
    void zeroHold(bool hold) override { holdScopeCore(hold); }
    bool zeroStopGenerator() override;
    bool zeroConfigure(const ScopeRequest &request) override;
    bool zeroAppliedAt(uint32_t &at) const override;
    uint32_t zeroSequence() const override { return configuration_.sequence; }
    bool zeroAcquire(scope::FrameLease &frame) override { return store_.acquireLatest(frame); }
    void zeroRestoreFailed(const ScopeRequest &original) override;
    bool zeroRetain() override { return retainScope(); }
    bool zeroRestoreHeld(const ScopeRequest &original) override { return restoreHeld(original); }
    void zeroReleaseRetained() override { releaseRetained(); }
    bool restoreHeld(const ScopeRequest &request); // restoreHeldScope for its owner (AutoSetup or the zero).
    void serviceZero(uint32_t now);
    bool configureScopeCore(const ScopeRequest &request,bool force=false,bool preserveRecord=false);
    void describeChannels(scope::FrameMetadata &metadata);
    // The DMM control word follows what the generator's output needs (generator::controlBits).
    void followMeterGenerator();
    // The capture's share of the bus with the ZT703S DAC stream (bus_load.cpp, Q-14).
    bool dacStreamStarting() override;
    void followBusLoad();
    bool yieldBus();
    bool withinBusLimit() const;
    // StorageAdmission::Capture (storage_yield.cpp): a NOR SPI transfer is about to start. Captures
    // of at most SlowTransfers DMA transfers per second keep running through it.
    bool yieldForStorage() override;
    static constexpr double SlowTransfers = 10e6;
    // A capture above SlowTransfers DMA transfers per second (both the storage yield and the USB quiet).
    static bool fastTransfers(double pointsPerSecond, unsigned pointsPerTransfer) {
        return pointsPerSecond / pointsPerTransfer > SlowTransfers;
    }
    // The per-channel maximum at the running clock: its profile's ADC divider (RunMode), halved
    // while the DAC generator loads the bus.
    uint32_t captureRateLimit() const;
    void holdScopeCore(bool hold);
    bool activate(MeasurementMode measurement);
    bool activateScope(); // The scope branch of activate: HOLD re-applied, otherwise a capture started.
    bool restartClockUsers(bool generator, bool measurement);
    bool startCapture();
    bool stopCapture(bool stopClock = false); // Also gives back the memory of an unpublished record.
    void releaseCaptureRegion();
    bool internalCapture() const;
    bool zeroTrial() const; // A zero calibration's trial phase: its captures alone are untriggered.
    bool reserveCapture(bool rolling, scope::CaptureBuffer &buffer);
    // Issue #89: a change of the trigger alone from Auto or Normal into Normal or Single keeps the
    // record on show for the view (acquireShownScope) when the capture has a second slot: it is from
    // before the arm, so acquireScope, the trigger inspection and hosts refuse it. A one-slot geometry
    // (Compact, Split, 512K) has no room for it: the store drops it as before.
    bool retainsShown(const ScopeRequest &next) const;
    // A record from before the arm of a Normal or Single capture (Auto takes any).
    bool beforeArm(uint32_t sequence) const {
        return request_.trigger.mode != scope::TriggerMode::Auto && int32_t(sequence - armSequence_) < 0;
    }
    bool samplePlan(scope::SamplePlan &plan) const;
    bool samplePlan(scope::SamplePlan &plan, const scope::CaptureBuffer &buffer, bool rolling) const;
    bool planCapture(scope::SamplePlan &plan, scope::CaptureBuffer &buffer, bool &rolling) const;
    scope::AcquisitionArena::Geometry recordGeometry() const;
    // ZT706S interleave (InterleavePolicy): whether a request gets it, the front end it then sets and
    // the frequency tracking for the range correction and gain (the HIGH ADC's correction:
    // interleaveOffset, public above).
    bool interleaving(const ScopeRequest &request) const;
    scope::FrontendSettings appliedFrontend(const ScopeRequest &request) const;
    scope::TriggerSettings triggerForFrame(const scope::FrameMetadata &metadata) const;
    void observeInterleave(const scope::FrameView &frame);
    bool describeNext(scope::FrameMetadata &metadata);
    void serviceScopeFrames(uint32_t now);
    void followTriggerWait(uint32_t now);
    void applyForce(); // forceScope() on the running capture, once it is one that can take it.
    // A record the trigger mode lets through: in Auto any, in Normal/Single a trigger or a forced capture.
    bool triggerAccepts(const scope::FrameMetadata &frame, bool found) const;
    void countTriggerEvent(bool accepted, const scope::FrameMetadata &frame); // scopeTriggerEvents.
    void serviceStreams(uint32_t now);
    // The ZT703S calibration reference's policy (GitHub #32), each pass: as the factory main firmware,
    // in Scope only (its DMM start never runs the output); never while stopping, across a clock change,
    // during a zero calibration or while the DMM serial output has the jack.
    bool referenceAllowed() const;
    void serviceScopeCapture(uint32_t now);
    static constexpr uint32_t ConfigQuietMs = 20;
    static constexpr double FullRateSamples = 100e6;
    bool capturePaused_ = false;
    static constexpr uint32_t FrameDemandMs = 500;
    uint32_t frameRequestedAt_ = 0xffff0000U;
    uint32_t configuredAt_ = 0xffff0000U; // Far from boot time: the first change is never a burst.
    bool findTrigger(const scope::FrameView &frame,uint32_t &index);
    void serviceRolling(uint32_t now);
    bool validateScope(const ScopeRequest &request) const;
    bool requestDmmKey(dmm::KeyCommand::Action action);
    // Shutdown with a stroke still owed (beginShutdown): its 21 goes before the UART stops, within
    // DmmCloseMs; past that, or a UART fault, the commands are abandoned and the UART stops all the same.
    void serviceDmmClose(uint32_t now);
    void stopDmmUart();
    void serviceDmmWatchdog(uint32_t now);
    void restartDmmUart();
    // The chip's commands are this session's: the DMM active, or the Scope's DMM trace running.
    bool meterLinked() const { return active_ && (measurement_ == MeasurementMode::Multimeter || meterTraceRunning_); }
    // The chip powered or not by what is in force: the DMM word (with the generator's bits), or in the
    // Scope bit 13 of its word.
    void powerMeter(bool on);
    bool startMeterUart(); // uart_ in the measurement's receive mode, its fault counts taken.
    bool receiveFaults();  // A byte lost since the last look: the framer starts again.
    // The next packet framed from uart_, reading at most `budget` bytes (counted down); each one
    // feeds the watchdog.
    bool framePacket(uint8_t (&packet)[dmm::Packet::Bytes], unsigned &budget);
    void serviceMeterTrace(uint32_t now);
    void stopMeterTrace();
    void traceReading(); // The session's newest reading into meterReadings_ (not held).
    void restartMeterLine(); // meterReadings_ in meterTraceFunction_ (meter_trace.cpp).
    void breakMeterTrace();
    bool meterColumn(const scope::FrameMetadata &record, ScopeRecorder::Meter &meter);
    ScopeRecorder::Meter meterHeader() const; // The function and quantity of a CSV's dmm column, no readings.
    bool meterTraceAsked_ = false, meterTraceRunning_ = false;
    // Readings were missed since the last one kept (HOLD, a restart): the next is preceded by a gap.
    bool meterGap_ = false;
    // The function last selected in the Scope, and the one meterReadings_ last started with.
    dmm::Function meterTraceFunction_ = dmm::Function::Unknown, meterLineFunction_ = dmm::Function::Unknown;
    uint32_t meterTraceStarts_ = 0;
    static dmm::ReadingTrace::Reading *meterTraceMemory(); // Its ITCM (.meter_trace), or nullptr (meter_trace.cpp).
    dmm::ReadingTrace meterReadings_{meterTraceMemory()};
    static_assert(dmm::LinkWatchdog::OffMs <= dmm::FunctionSelector::PowerOnWaitMs, "A selection's deadline covers the power-on wait");
    dmm::LinkWatchdog watchdog_; // Silence power-cycles, the minimum off time, the UART's bounded restarts.
    static constexpr uint32_t DmmCloseMs = 500;
    bool dmmClosing_ = false;
    uint32_t dmmCloseDeadline_ = 0;
    PeripheralResources resources_;
    // Puts a changed calibration in force: a running capture restarts at once (both caches see the
    // offsets' generation; the description takes gain and interleave); in HOLD the next RUN does.
    void calibrationChanged();
    bool calibrate(uint32_t now,calibration::ZeroProcedure &procedure);
    DataStatus interleaveChange() const;
    CalibrationData calibration_; // The factory record: read-only.
    FactoryRecordPolicy factoryRecordPolicy_ = FactoryRecordPolicy::Auto; // The one this boot read the record with.
    calibration::ScopeCalibration scopeCalibration_{calibration_};
    calibration::ZeroFile zeroFile_;
    calibration::PwmFile pwmFile_;
    calibration::ZeroCalibration softwareZero_{zeroFile_,scopeCalibration_};
    calibration::PwmZeroCalibration hardwareZero_{scopeCalibration_,pwmFile_};
    calibration::GainFile gainFile_;
    calibration::InterleaveFile interleaveFile_;
#if OPENOSC_BOARD == 703
    calibration::InterleaveMatchProfiles interleaveMatchProfiles_;
    bool matchCalibrating_ = false;
    const scope::InterleaveMatch *matchFor(const scope::FrameMetadata &metadata,
                                          const scope::FrontendSettings &frontend, const Rational &rate) const;
#endif
    calibration::GainCalibration gain_{scopeCalibration_,gainFile_};
    calibration::ZeroOperation zeroOperation_;
    uint32_t frontendGeneration_=0; // Of the offsets the running capture was configured with.
    bool zeroBlocked_=false;
    uint32_t zeroMinimumSequence_=0;
    scope::AnalogFrontend frontend_;
    uint32_t zeroApplies_=0; // frontend_.applies() before the zero's last configure.
    scope::CaptureDma capture_;
    scope::AcquisitionArena arena_;
    scope::FrameStore store_{resources_, arena_};
    scope::PulseCounters counters_;
    scope::RollingBuffer rolling_{scope::AcquisitionArena::rollHistory(),scope::AcquisitionArena::RollWindow};
    scope::TrendBuffer trend_{scope::AcquisitionArena::trendMemory()};
    uint8_t captureRegion_ = 0, retainedRegion_ = 0;
    scope::FrameView retained_;
    scope::FrameLease inspected_; // DataAccess's own look at a new frame, until the end of poll.
    bool trials_ = false, holdPending_ = false, captureWaiting_ = false;
    bool forcePending_ = false;   // forceScope() not yet taken by a capture that can complete it.
    // stageScope() changed the request after the running (or last) capture started: that capture's
    // record is the old plan, so leaving HOLD restarts instead of keeping it (Root HIL 2026-11-13).
    bool staged_ = false;
    uint32_t forcedSequence_ = 0; // The capture forced last, until its record is adopted (0: none).
    uint32_t acceptedSequence_ = 0; // scopeFrameSequence().
    uint32_t triggerEvents_ = 0;    // scopeTriggerEvents().
    // The newest capture sequence that cannot step it: the last counted, or the last started at an
    // arm (its record is from before; restartTriggerEvents).
    uint32_t triggerFloor_ = 0;
    void restartTriggerEvents() { triggerFloor_ = sequence_; }
    uint32_t acceptedAt_ = 0;       // When it was accepted (FrameHosts::HoldMs).
    FrameHosts frameHosts_;
    bool hostOwed(uint32_t now) const; // The record on show is accepted and still owed to a USB host.
    uint32_t holdDeadline_ = 0;
    static constexpr uint32_t HoldWaitMs = 2000;
    dmm::UartDma uart_;
    board::GeneratorOutput generator_;
    board::DmmSerialOutput serial_;
    bool clockSuspended_ = false, generatorRetime_ = false, serialRetime_ = false;
    uint16_t meterGeneratorBits_ = 0; // generator::controlBits in the DMM word now.
    // busLoad_: the bus load the plans follow; busAnnounced_: a DAC stream starts right now.
    bool busLoad_ = false, busAnnounced_ = false;
    dmm::PacketFramer framer_;
    dmm::Session meter_;
    dmm::MeterCommands dmm_; // Every DMM command on uart_ (selector, keys, range, the AUTO/RANGE stroke).
    ScopeRecorder recorder_;
    DmmCsvLogger logger_;
#ifdef OPENOSC_APPLICATION
    DmmSessionRecorder &session_; // ITCM (dmmSessionRecorder): not in main()'s frame.
#else
    DmmSessionRecorder session_;
#endif
    dmm::Function meterFunction() const; // The function a reading is of (Type K: Temperature).
    scope::FrameLease heldFrame_;
    scope::HeldTrigger heldTrigger_;
    scope::InterleaveTracker interleave_; // ZT706S: CH1's frequency, range correction and gain (InterleaveTracker).
    bool interleaveHeadroom_ = false; // ZT703S: a higher physical range after sustained clipping.
    scope::InterleaveConditions::Inputs interleaveInputs(const ScopeRequest &request) const;
    float recordInterleaveOffset() const;
    // The ZT706S factory interleave and its curves (range correction, display gain); never the ZT703S's.
    static constexpr bool factoryInterleave() { return board::Current::Features.adcInterleave; }
    scope::InterleaveMode interleaveMode_ = factoryInterleave() ? scope::InterleaveMode::Auto : scope::InterleaveMode::Off;
    calibration::InterleaveOffsetMeasurement interleaveMeasure_;
    bool interleaveRestart_ = false;
    uint32_t interleaveGeneration_ = 0; // The front-end offsets' generation a running measurement began under.
    void serviceInterleaveOffset(uint32_t now);
    void finishInterleaveOffset();
    void endInterleaveOffset(calibration::InterleaveOffsetMeasurement::Result result);
    // Whether a running offset measurement still measures what it began on, were `request` captured now.
    bool interleaveMeasureHolds(const ScopeRequest &request) const;
    bool saveInterleaveProfile(const calibration::InterleaveProfile &profile); // The file (zero_calibration.cpp).
#ifdef OPENOSC_INTERLEAVE_PROBE
    scope::InterleaveProbe probe_;
    scope::InterleaveProbe::Inputs probeInputs(const ScopeRequest &request) const;
#endif
    ScopeRequest request_;
    scope::FrameMetadata configuration_;
    uint64_t rollingConsumed_=0,rollingPublished_=0;
    uint32_t rollingAt_=0;
    bool rollingActive_=false,rollingLost_=false;
    // serviceBackground is running (a frame is being drawn): the trend is written by poll alone.
    bool backgroundPass_=false;
    scope::TriggerCache triggerCache_;
    scope::FrameLease readout_;
    uint32_t readoutRevision_ = 0;
    scope::BackgroundReader *background_ = nullptr; // The decoder's job (ITCM); its lease is its own.
    void revokeBackground() { releaseReadout(); if (background_) background_->revoke(); }
    MeasurementMode measurement_ = MeasurementMode::Oscilloscope;
    DataStatus status_ = DataStatus::NoData;
    uint32_t holdReleases_ = 0;
    // A capture fault (HardwareError, Timeout, Overflow) is owed a restart until a start succeeds: HOLD
    // waits for that restart (holdScopeCore) also in the pass its retry came due but could not start.
    bool captureRestartOwed_ = false;
    bool initialized_ = false, active_ = false, stopping_ = false, scopeHeld_ = false, singleDone_ = false, captureFailed_ = false,
         recordPending_ = false;
    ScopeRecorder::Format recordFormat_ = ScopeRecorder::Format::Raw;
    uint32_t sequence_ = 0, previousTick_ = 0, receiveErrors_ = 0, receiveOverflows_ = 0;
    uint64_t timeUs_ = 0;
    uint32_t armSequence_ = 0, captureRetryAt_ = 0;
    static constexpr uint32_t CaptureRetryMs = 50;
    bool triggerWaiting_ = false, acceptedTriggered_ = false; // acceptedTriggered_: TRIG:STAT? TD.
    uint64_t dmmActivatedAtUs_ = 0, recordDeadlineUs_ = 0;
    DataStatus recordStatus_ = DataStatus::NoData;
    // followTriggerWait: since when (armedAt_) the capture armedSequence_ has been seen Armed.
    static constexpr uint32_t TriggerWaitMs = 250;
    uint32_t armedSequence_ = 0, armedAt_ = 0;
    bool armedSeen_ = false;
    // A qualified trigger's unqualified candidates rearm a new capture each (CaptureDma::misses): Armed
    // never lasts, so WAIT follows TriggerWaitMs of them without an accepted record.
    uint32_t missesSeen_ = 0;
    bool usbQuiet_ = false; // quietCapture(true): no fast capture starts.
};
} // namespace openosc
