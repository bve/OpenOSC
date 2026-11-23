#pragma once
#include "openosc/acquisition/request.hpp"
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/acquisition/progress.hpp"
#include "openosc/generator/waveform.hpp"
#include "openosc/measurement/dmm/ranges.hpp"
#include "openosc/measurement/dmm/session.hpp"
#include "openosc/instrument/mode.hpp"
#include "openosc/data/memory.hpp"
#include "openosc/data/performance.hpp"
#include "openosc/platform/idle_policy.hpp"

namespace openosc::scpi {
// The DMM hold modes as the PC sees them (docs/DMM_AUTO_HOLD.md): the saved settings and the cycle.
struct MeterHold {
    dmm::HoldSettings settings;
    dmm::HoldDetector::Phase phase = dmm::HoldDetector::Phase::Idle;
    bool captured = false; // A snapshot is what meter() reports now.
    uint32_t captures = 0, remainingMs = 0;
};
// The protocol decoder as DECode: reports it (docs/SCPI.md): its switch, protocol and the published
// result with its full record key, counters and Fs. Strings are static.
struct DecodeReport {
    bool enabled = false, result = false;
    const char *protocol = "OFF", *status = "";
    uint32_t events = 0, lost = 0, dropped = 0, overflows = 0, clipped = 0, processed = 0, samples = 0;
    uint32_t generation = 0, sequence = 0, origin = 0, fingerprint = 0, settingsHash = 0, fsNumerator = 0, fsDenominator = 0;
    uint32_t revision = 0;
    uint64_t timestampUs = 0;
};
// Called only by the foreground dispatcher. A frame remains leased until releaseFrame().
class Backend {
public:
    virtual ~Backend() = default;
    virtual bool multimeter() const = 0;
    virtual const ScopeRequest &scopeRequest() const = 0;
    virtual const scope::FrameMetadata *configuration() const = 0;
    virtual bool configureScope(const ScopeRequest &) = 0;
    // The same, with its trigger level set on purpose (TRIG:EDGE:LEV): an instrument that keeps the
    // trigger's voltage across scales takes it as the new anchor, also as the same whole code.
    virtual bool setTrigger(const ScopeRequest &request) { return configureScope(request); }
    // CHANnel<n>:DISPlay: the channel shown, and on or off as the panel switches it (the last one shown
    // goes off only with the DMM trace on, then hidden: plot::ChannelDisplay); 0 or the SCPI error.
    virtual bool channelShown(unsigned channel) const { return scopeRequest().frontend.enabled[channel]; }
    virtual int16_t showChannel(unsigned channel, bool on) {
        auto request = scopeRequest();
        return !request.setChannel(channel, on) ? -221 : configureScope(request) ? 0 : -222;
    }
    // DMM:FUNCtion: the function of the Scope's DMM trace (a MeterFunctions index, the DMM screen's too),
    // set as its channel menu's Function row sets it; 0 or the SCPI error. Without one: none.
    virtual unsigned meterTraceFunction() const { return ~0U; }
    virtual int16_t meterTraceFunction(unsigned) { return -221; }
    virtual bool run(bool single) = 0;
    virtual bool autoScale() { return false; }
    // A forced trigger (TFORce): a record without a trigger event (DataAccess::forceScope). False:
    // refused (not Scope, HOLD or a done Single, zero/AutoSetup busy, shutdown): -221.
    virtual bool forceTrigger() { return false; }
    // The sequence of the newest frame a reader is given (finite or Roll), 0 for none; takes no lease.
    virtual uint32_t frameSequence() const { return 0; }
    virtual void stop() = 0;
    virtual bool held() const = 0;
    virtual bool waiting() const = 0;
    virtual bool triggered() const { return false; } // The last frame held its trigger edge (TRIG:STAT? TD).
    virtual const scope::FrameView *frame(bool refresh) = 0;
    virtual void releaseFrame() = 0;
    // The session ended (Engine::reset: a new session, a disconnect, a clear): its frame lease and
    // any claim on records it has not read yet go.
    virtual void endSession() { releaseFrame(); }
    virtual const dmm::Result &meter() const = 0;
    virtual DataStatus meterStatus() const = 0;
    virtual bool selectFunction(dmm::Function) = 0;
    virtual dmm::TemperatureSettings meterTemperature() const { return {}; }
    virtual bool meterTemperature(dmm::TemperatureSettings) { return false; }
    virtual bool setAuto(bool) = 0;
    virtual bool setRange(unsigned) = 0;
    // The panel's HOLD and REL, and the statistics of the DMM screen (Session: one owner, the same
    // period the screen shows). meter() already reports both states (packetHold, peerRelative).
    virtual bool holdMeter(bool) = 0;
    virtual bool relativeMeter(bool) = 0;
    virtual dmm::StatisticsView meterStatistics() const = 0; // As the DMM screen shows it.
    // The hold modes: their settings (saved and applied as Measurement > Hold does: a running cycle
    // ends, none starts; false: not taken), the cycle armed as the HOLD key arms it (false: Manual,
    // not the DMM, a manual HOLD in force), and the state. meter() reports a snapshot as held.
    virtual MeterHold meterHold() const { return {}; }
    virtual bool meterHold(const dmm::HoldSettings &) { return false; }
    virtual bool armMeterHold() { return false; }
    virtual void resetMeterStatistics() = 0;
    // The MODE screen, as the MODE key selects it. A different measurement (Scope <-> DMM) changes the
    // USB profile: the device reconnects, as after MODE on the panel.
    virtual MeasurementMode screen() const = 0;
    virtual bool screen(MeasurementMode) = 0;
    // SYSTem:KEY: the panel key `key` (Keypad::Key) held for `ms` as a person would hold it, through the
    // real input path. False: an earlier one is still held or its events not taken yet, or no keys. busy() holds while it is.
    static constexpr uint32_t KeyMinMs = 30, KeyMaxMs = 10000; // 30 ms clears the 25 ms debounce.
    virtual bool pressKey(uint32_t, uint32_t) { return false; }
    virtual bool busy() const = 0;
    virtual uint32_t operationTimeoutMs() const { return 10000; }
    virtual DataStatus operationStatus() const = 0;
    virtual generator::Snapshot generator() const = 0;
    virtual bool configureGenerator(const generator::Settings &) = 0;
    virtual bool output(bool enabled) = 0;
    // SOURce:ARBitrary (ZT703S AWG, docs/AWG.md): a session's definite block of int16 DAC codes, staged
    // as it comes (`owner`: the session; one stage at a time on the instrument). begin: 0, BlockSink::
    // Wait (this session's earlier block is not taken yet) or the error (-200 another session's or
    // the CSV loader's stage, -221 no free bank or another operation owns the output, -241 no AWG);
    // byte: each data byte; commit: 0 or the error, the stage ends either way; abort: it ends.
    // read: the loaded waveform's bytes (little-endian int16) from `offset`, 0 once `revision` (the
    // generator::Snapshot's) is no longer loaded.
    virtual int16_t arbitraryBegin(const void *, uint32_t) { return -241; }
    virtual void arbitraryByte(const void *, uint8_t) {}
    virtual int16_t arbitraryCommit(const void *) { return -241; }
    virtual void arbitraryAbort(const void *) {}
    virtual size_t arbitraryRead(uint32_t, uint32_t, uint8_t *, size_t) { return 0; }
    // Startup output (GeneratorStartup): true restores the last confirmed output at boot, false off.
    virtual bool generatorStartup() const = 0;
    virtual bool generatorStartup(bool restore) = 0;
    virtual bool remote() const = 0;
    virtual bool remote(bool enabled) = 0;
    virtual bool performance(PerformanceSnapshot &) const { return false; }
    // Interleave CH1 (ZT703S, ACQ:INTerleave): whether the board has the choice, the choice (saved like
    // the menu's), whether the request in force interleaves now and what keeps it from it
    // (InterleaveConditions bits). Without a backend for it: unsupported, off accepted.
    struct InterleaveStatus {
        bool supported = false, requested = false, active = false;
        uint32_t blockers = 0;
        bool autoSupported = false, automatic = false;
    };
    virtual InterleaveStatus interleave() const { return {}; }
    virtual bool interleave(bool on) { return !on; }
    virtual bool interleaveAuto() { return false; }
    // The GUI heap, read now (a pool walk); false while there is none to read.
    virtual bool memory(MemorySnapshot &) const { return false; }
    // SYST:STALl?: long main-loop passes and Auto frame gaps (StallLog::text).
    virtual size_t stalls(char *out, size_t size) const { if (size) *out = 0; return 0; }
    static constexpr size_t BootTextBytes = 256; // SYST:BOOT?'s reply buffer.
    virtual size_t bootTimes(char *out, size_t size) const { if (size) *out = 0; return 0; } // SYST:BOOT? (BootTiming).
    virtual size_t meterPacket(const uint8_t *&data) const { data = nullptr; return 0; } // SYST:DMM:PACK?
    // DISP:DATA?: the screen copied now into the preview buffer, leased under a ticket (0: refused, no
    // display or the buffer in use). screenRead answers 0 for a ticket that is no longer the lease
    // (expired and taken by a screenshot, an image or another stream): the engine then pads with -230.
    virtual uint32_t screenCapture() { return 0; }
    virtual size_t screenRead(uint32_t, uint32_t, uint8_t *, size_t) { return 0; }
    virtual void screenEnd(uint32_t) {} // The stream finished or was dropped: that lease is free.
    // The factory calibration record as this boot decoded it (SYST:CAL:FACT?, docs/SCPI.md), from the
    // record alone: no plan, user gain, display gain, channel state or calibration in progress.
    // identity: the CRC calibration files are bound to; gainWord: as decoded (0: not written, also every
    // compact record); scale: whether the record gives the channel a scale.
    struct FactoryCalibration { bool valid = false; uint16_t identity = 0, gainWord[2] = {0, 0}; bool scale[2] = {false, false}; };
    virtual bool factoryCalibration(FactoryCalibration &) const { return false; }
    // SYST:CAL:FACT:FORM?: the policy this boot read the record with (AUTO, LONG, COMPACT; not a later
    // settings change) and the resolved format (LONG, COMPACT, AMBIGUOUS; INVALID without a record).
    struct FactoryRecordReading { const char *policy = "AUTO", *format = "INVALID"; };
    virtual bool factoryRecordFormat(FactoryRecordReading &) const { return false; }
    virtual bool recovery() { return false; }
    // Run mode as the core clock in MHz (0: no profile runs, the HSI fallback). The setter changes
    // it in place, as the Settings menu does, and returns 0 or the SCPI error to report.
    virtual uint32_t coreMhz() const { return 0; }
    virtual int coreMhz(uint32_t) { return -241; }
    virtual bool counter(unsigned,scope::CounterReading &) const { return false; }
    // Analyze > Measurements > Short window f/T (GitHub #171): WAV:MEAS? applies it as the readouts do.
    virtual bool shortWindowCounter() const { return false; }
    // SYST:POW:TIM, SYST:DISP:TIM/DIM: the Settings menu's idle choices, saved as it saves them (0 or the error).
    virtual bool idleSettings(IdleSettings &) const { return false; }
    virtual int setIdle(const IdleSettings &) { return -241; }
    // MMEMory (paths checked by the engine): 0 or the SCPI error; fileRead 0: gone.
    virtual int fileList(const char *, uint32_t, char *out, size_t size, uint32_t &count, uint32_t &total) { if (size) *out = 0; count = total = 0; return -241; }
    virtual int fileOpen(const char *, uint32_t &) { return -241; }
    virtual size_t fileRead(uint32_t, uint8_t *, size_t) { return 0; }
    virtual void fileClose() {}
    virtual int fileDelete(const char *) { return -241; }
    virtual scope::CaptureProgress captureProgress() const { return {}; }
    // DECode: (decoder builds). report: false when the build has no decoder. One CSV export at a time
    // (DECode:DATA?): begin gives its ticket (0: `error` -200 busy, -230 no finished result), measure()
    // its size (1 known, 0 not yet, -1 cancelled), read() its bytes (0 before the end: cancelled), end()
    // lets it go; the engine keeps only the ticket. save(): DECode:SAVE (0 started, else the error);
    // saved(): the last file, "BUSY" while one is written, or why the last one failed.
    // WAV:KEY?: the full record key of the record frame() leased ("generation,sequence,timestamp_us,
    // origin,samples,0xFINGERPRINT": every field of RecordKey, as DECode:RESult? has them), frozen when
    // it was leased. False: none leased.
    virtual bool frameKey(char *, size_t) const { return false; }
    virtual bool decodeReport(DecodeReport &) const { return false; }
    virtual uint32_t decodeExport(int16_t &error) { error = -113; return 0; }
    virtual int decodeMeasure(uint32_t, uint32_t &) { return -1; }
    virtual size_t decodeRead(uint32_t, uint8_t *, size_t) { return 0; }
    virtual void decodeEnd(uint32_t) {}
    virtual int16_t decodeSave() { return -113; }
    virtual const char *decodeSaved() const { return ""; }
    virtual const char *serial() const = 0;
    virtual const char *version() const = 0;
};
} // namespace openosc::scpi
