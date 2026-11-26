#include "openosc/board/current.hpp"
#include "openosc/application/boot_timing.hpp"
#include "openosc/application/scpi_backend.hpp"
#include "openosc/screenshot/reader.hpp"
#include <cstring>
#include <strings.h>
#include "openosc/application/controller.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/input_service.hpp"
#include "openosc/platform/clocks.hpp"
#include <cstdio>
#include <iterator>
#include <strings.h>
#include "openosc/storage/open_readers.hpp"

namespace openosc {
InstrumentBackend::InstrumentBackend(ApplicationController &application)
    : application_(application), host_(application.data_.frameHosts().attach()) {
    const auto *uid = reinterpret_cast<const uint32_t*>(UID_BASE); constexpr char hex[] = "0123456789ABCDEF";
    for (unsigned w = 0; w < 3; ++w) for (unsigned i = 0; i < 8; ++i) serial_[8*w+i] = hex[(uid[w] >> (28-4*i)) & 15];
}
bool InstrumentBackend::multimeter() const { return application_.data_.measurementMode() == MeasurementMode::Multimeter; }
const ScopeRequest &InstrumentBackend::scopeRequest() const { return application_.data_.scopeRequest(); }
const scope::FrameMetadata *InstrumentBackend::configuration() const {
    if (application_.data_.zeroBusy()) return nullptr;
    const auto &metadata = application_.data_.scopeMetadata();
    return metadata.valid() ? &metadata : nullptr;
}
// A Bode sweep's settings in force (bodeOwns, both boards): the generator, the capture and its records are the
// sweep's; the paths that would change them or hold its records refuse until they are restored.
bool InstrumentBackend::configure(const ScopeRequest &r, bool level) {
    if (application_.data_.zeroBusy() || application_.bodeOwns()) return false;
    auto &a = application_; if (!a.cancelAuto(Board::milliseconds())) return false;
    return a.applyFollowing(r,Board::milliseconds(),ApplicationController::ScopeApply::StageIfHeld,
                            level ? ApplicationController::LevelIntent::Set : ApplicationController::LevelIntent::Follow);
}
bool InstrumentBackend::configureScope(const ScopeRequest &r) { return configure(r, false); }
bool InstrumentBackend::setTrigger(const ScopeRequest &r) { return configure(r, true); }
void InstrumentBackend::demand() const { application_.data_.frameHosts().demand(host_, Board::milliseconds()); }
bool InstrumentBackend::run(bool single) {
    if (application_.data_.zeroBusy() || application_.bodeOwns()) return false;
    application_.data_.frameHosts().rearm(host_, application_.data_.scopeFrameSequence(), Board::milliseconds());
    // The decoded record kept for hosts is of before the new arm (and a share skips acquireScope's Single
    // stale guard): it goes; the new arm's records are shared again once decoded.
    application_.decoder_.dropHostRecord();
    auto &a = application_; if (!a.cancelAuto(Board::milliseconds())) return false;
    if (single) {
        auto r = scopeRequest(); r.trigger.mode = scope::TriggerMode::Single;
        if (!a.applyScope(r,Board::milliseconds(),ApplicationController::ScopeApply::Resume)) return false;
    } else if (!a.data_.runScope()) return false; // -221: HOLD and its record stay.
    a.dataRefresh_ = true; return true;
}
bool InstrumentBackend::autoScale() {
    auto &a=application_;
    if (a.data_.measurementMode()!=MeasurementMode::Oscilloscope || a.data_.zeroBusy() ||
        a.data_.scopeRecording() || a.autoBusy() || a.bodeOwns()) return false;
    a.startAuto(Board::milliseconds());
    return a.autoBusy();
}
// TFORce: the acquisition's forced record; AutoSetup trials are its own refusal, zero/HOLD/Single
// done are DataAccess's.
bool InstrumentBackend::forceTrigger() {
    auto &a = application_;
    if (a.data_.measurementMode() != MeasurementMode::Oscilloscope || a.autoBusy() || a.bodeOwns()) return false;
    demand(); // The forced record is read next (Root HIL 2026-11-13: Normal TFOR lost it to the next capture).
    return a.data_.forceScope();
}
uint32_t InstrumentBackend::frameSequence() const {
    const auto &a = application_;
    const auto &d = a.data_;
    if (d.measurementMode() != MeasurementMode::Oscilloscope || d.zeroBusy() || a.bodeOwns()) return 0;
    demand(); // ACQ:FRAME?: the host polls for the record it will read.
    if (!decodedRecords()) return d.scopeFrameSequence();
    // The decoder on (plan §5.3): the record record() serves: the decoded one kept for hosts, else the
    // newest only when it is the decoded one; 0 while none is (the host polls again).
    if (const auto *shared = a.decoder_.hostRecord()) return shared->metadata().sequence;
    const auto *published = a.decoder_.results().published();
    return published && published->key().sequence == d.scopeFrameSequence() ? published->key().sequence : 0;
}
// In RUN with the decoder on, a host reads only decoded records (UI and USB on the same record, all
// three views); in HOLD the held record and its result share their key: as before. Off: as before.
bool InstrumentBackend::decodedRecords() const {
    return application_.prefs_.scope.decoder.enabled && !application_.data_.scopeHeld();
}
bool InstrumentBackend::record(scope::FrameLease &out) {
    auto &a = application_;
    if (!decodedRecords()) return a.data_.acquireScope(out);
    if (a.decoder_.takeHostRecord(out)) return true; // The decoded record kept for hosts.
    scope::FrameLease raw;
    if (!a.data_.acquireScope(raw)) return false;
    if (!a.decoder_.shows(raw.view().metadata())) return false; // Not decoded (yet): nothing, not taken.
    out = std::move(raw);
    return true;
}
void InstrumentBackend::stop() {
    auto &a=application_;
    if (a.bodeOwns()) return;
    if (!a.cancelAuto(Board::milliseconds(),true)) return;
    a.data_.holdScope(true); a.dataRefresh_=true;
}
bool InstrumentBackend::held() const { return application_.data_.scopeHeld(); }
bool InstrumentBackend::waiting() const { return application_.data_.scopeWaiting(); }
bool InstrumentBackend::triggered() const { return application_.data_.scopeTriggered(); }
const scope::FrameView *InstrumentBackend::frame(bool refresh) {
    if (refresh) releaseFrame();
    if (application_.bodeOwns()) { releaseFrame(); return nullptr; } // Not a sweep's record: its lease would hold capture.
    application_.data_.noteFrameDemand(Board::milliseconds());
    demand();
    if (!lease_.valid() && !record(lease_)) return nullptr;
    const auto &m = lease_.view().metadata();
    // Taken also when refused below: that refusal means this host read exactly this record already, so
    // it is owed it no more (else FrameHosts would hold the next capture for it, HoldMs in one slot). A
    // record not served by record() (not decoded yet) is never marked taken.
    application_.data_.frameHosts().taken(host_, m.sequence);
    if (freshFrame_ && !application_.data_.scopeHeld() && m.sequence == previousFrameSequence_ && m.timestampUs == previousFrameTime_) {
        lease_.release(); return nullptr; // Leave the store writable until a newer DMA frame is published.
    }
    freshFrame_ = false;
    leaseKey_ = decoding::RecordKey::of(m, application_.decoder_.generation()); // Frozen with the lease.
    return &lease_.view();
}
void InstrumentBackend::releaseFrame() {
    if (lease_.valid()) {
        previousFrameSequence_ = lease_.view().metadata().sequence;
        previousFrameTime_ = lease_.view().metadata().timestampUs;
        freshFrame_ = true;
    }
    lease_.release();
}
void InstrumentBackend::endSession() {
    releaseFrame();
    application_.data_.frameHosts().end(host_);
}
const dmm::Result &InstrumentBackend::meter() const {
    const auto &session = application_.data_.meter(); reading_ = session.displayed();
    if (reading_.primary.valid() && session.relative().active()) reading_.primary = session.relative().apply(reading_);
    reading_.packetHold = session.holding(); // A HOLD's or a hold mode's reading: READ waits for none.
    reading_.peerRelative = reading_.peerRelative || session.relative().active();
    return reading_;
}
DataStatus InstrumentBackend::meterStatus() const { return application_.data_.meter().status(application_.data_.nowUs(),2000000); }
bool InstrumentBackend::selectFunction(dmm::Function f) { return application_.meterCommand(application_.data_.selectDmmFunction(f)); }
dmm::TemperatureSettings InstrumentBackend::meterTemperature() const { return application_.prefs_.meter.temperature; }
bool InstrumentBackend::meterTemperature(dmm::TemperatureSettings settings) {
    return application_.meterTemperatureSettings(settings, Board::milliseconds());
}
bool InstrumentBackend::setAuto(bool value) { return application_.meterCommand(application_.data_.setDmmAuto(value)); }
bool InstrumentBackend::setRange(unsigned index) { return application_.meterCommand(application_.data_.setDmmRange(index)); }
bool InstrumentBackend::holdMeter(bool held) { return application_.data_.holdMeter(held); }
bool InstrumentBackend::relativeMeter(bool enabled) { return application_.data_.relativeMeter(enabled); }
scpi::MeterHold InstrumentBackend::meterHold() const {
    const auto &cycle = application_.data_.meter().holdCycle();
    return {application_.prefs_.meter.hold, cycle.phase(), cycle.captured() != nullptr, cycle.captures(),
            cycle.remainingMs(application_.data_.nowUs())};
}
bool InstrumentBackend::meterHold(const dmm::HoldSettings &settings) {
    return application_.meterHoldSettings(settings, Board::milliseconds());
}
bool InstrumentBackend::armMeterHold() { return application_.data_.armMeterHold(); }
dmm::StatisticsView InstrumentBackend::meterStatistics() const {
    const auto &session = application_.data_.meter();
    const auto &shown = session.displayed();
    return dmm::StatisticsView::of(session.statistics(), shown.primary.quantity, session.relative().offset(shown));
}
void InstrumentBackend::resetMeterStatistics() { application_.data_.meter().resetStatistics(); }
MeasurementMode InstrumentBackend::screen() const { return application_.state_.mode; }
bool InstrumentBackend::screen(MeasurementMode mode) {
    return application_.selectMode(mode, Board::milliseconds()) == ApplicationController::ModeChange::Selected;
}
// The DMM entry's restore counts while it is owed (meterRestorePending): *OPC? does not report
// complete between the function's confirmation and the range command it still triggers.
bool InstrumentBackend::pressKey(uint32_t key, uint32_t ms) { return InputService::inject(key, ms); }
bool InstrumentBackend::busy() const {
    return application_.data_.dmmBusy() || application_.meterRestorePending() || application_.data_.zeroBusy() || application_.autoBusy() ||
           InputService::injecting();
}
uint32_t InstrumentBackend::operationTimeoutMs() const {
    if (application_.autoBusy()) return 602000;
    if (application_.data_.zeroBusy())
        // The procedure's own deadline, then the restore (12 s) and a new capture of the original sweep.
        return application_.data_.zeroOperation().calibration().durationMs()+14000+
               uint32_t(application_.data_.zeroOperation().calibration().original().timebaseNs/100000);
    if (application_.meterRestorePending()) return MeterRestore::ChainMs + 2000; // Selection + restore, and a packet.
    if (InputService::injecting()) return KeyMaxMs + 2000; // The longest SYSTem:KEY hold, and a packet.
    return 10000;
}
DataStatus InstrumentBackend::operationStatus() const { return application_.data_.dmmCommandStatus(); }
generator::Snapshot InstrumentBackend::generator() const { return application_.data_.generator().snapshot(); }
bool InstrumentBackend::configureGenerator(const generator::Settings &s) {
    return !application_.bodeOwns() && application_.applyGenerator(s,Board::milliseconds());
}
bool InstrumentBackend::output(bool value) { return !application_.bodeOwns() && application_.requestGeneratorOutput(value, Board::milliseconds()); }
scpi::Backend::InterleaveStatus InstrumentBackend::interleave() const {
    const auto &a = application_;
    const uint32_t blockers = a.data_.interleaveBlockers();
    const auto &features = board::Current::Features;
    const auto mode = scope::InterleaveMode(a.prefs_.scope.interleave);
    return {features.interleaveControl(), mode == scope::InterleaveMode::On, !blockers, blockers,
            features.interleaveControl(), mode == scope::InterleaveMode::Auto}; // Auto: both boards' own rule.
}
bool InstrumentBackend::interleave(bool on) {
    if (application_.bodeOwns()) return false;
    return application_.selectInterleave(on ? scope::InterleaveMode::On : scope::InterleaveMode::Off, Board::milliseconds());
}
bool InstrumentBackend::interleaveAuto() { return !application_.bodeOwns() && application_.selectInterleave(scope::InterleaveMode::Auto, Board::milliseconds()); }
bool InstrumentBackend::generatorStartup() const { return application_.prefs_.generatorStartup; }
bool InstrumentBackend::generatorStartup(bool restore) {
    auto &a = application_;
    if (a.prefs_.generatorStartup != unsigned(restore)) { a.prefs_.generatorStartup = restore; a.changed(Board::milliseconds()); }
    return true;
}
bool InstrumentBackend::remote() const { return application_.remote_; }
bool InstrumentBackend::remote(bool value) { return application_.setRemote(value); }
bool InstrumentBackend::recovery() {
    auto &a=application_;
    if (a.recoveryPending_ || a.shutdown_!=ShutdownAction::None || a.shots_.busy() || a.data_.zeroBusy()) return false;
    a.recoveryPending_=true; a.recoveryAt_=Board::milliseconds()+250;
    return true;
}
uint32_t InstrumentBackend::coreMhz() const {
    const uint32_t mode = ClockControl::activeMode();
    return mode < RunModeCount ? RunModes[mode].coreHz / 1000000 : 0;
}
int InstrumentBackend::coreMhz(uint32_t mhz) {
    uint32_t mode = 0;
    while (mode < RunModeCount && RunModes[mode].coreHz != mhz * 1000000ULL) ++mode;
    if (mode == RunModeCount) return -224; // Only the profiles of the Settings menu.
    if (!RemoteClock::allowed(mode, application_.remote_)) return -221; // Remote keeps 200 MHz.
    if (application_.bodeOwns()) return -221; // The generator and the capture are the sweep's (the DAC's clock too).
    switch (application_.changeRunMode(mode, Board::milliseconds(), RemoteClock::persists(application_.remote_)).outcome) {
    case RunModeSwitch::Outcome::Applied:
    case RunModeSwitch::Outcome::Unchanged: return 0;
    case RunModeSwitch::Outcome::Refused: return -221;    // Busy: nothing changed.
    default: return -200;                                  // Not applied, or users not restarted: query says what runs.
    }
}
bool InstrumentBackend::idleSettings(IdleSettings &settings) const { settings = application_.prefs_.idle; return true; }
int InstrumentBackend::setIdle(const IdleSettings &settings) {
    auto &a = application_;
    if (!settings.valid()) return -222;
    const auto now = Board::milliseconds();
    a.dropEdit(now); // An unconfirmed edit is undone first, never saved with this.
    auto chosen = a.prefs_; chosen.idle = settings;
    a.applySettings(chosen, now);
    if (a.state_.settingsOpen) a.showSettings();
    return 0;
}
int InstrumentBackend::fileList(const char *folder, uint32_t start, char *out, size_t size, uint32_t &count, uint32_t &total) {
    count = total = 0; if (size) *out = 0;
    if (!application_.fs_.mounted()) return -250;
    DIR dir{}; FILINFO info{};
    if (const FRESULT opened = f_opendir(&dir, folder); opened != FR_OK) return opened == FR_NO_PATH ? 0 : -256; // Not made yet: empty.
    size_t used = 0; bool full = false;
    FRESULT result;
    while ((result = f_readdir(&dir, &info)) == FR_OK && info.fname[0]) {
        if (info.fattrib & (AM_DIR | AM_HID | AM_SYS)) continue;
        const char *dot = std::strrchr(info.fname, '.');
        if (dot && !strcasecmp(dot, ".TMP")) continue; // Still written.
        if (total++ < start || full) continue;
        const int n = std::snprintf(out + used, size - used, "%s\"%s\",%lu", used ? "," : "", info.fname, (unsigned long)info.fsize);
        if (n < 0 || size_t(n) >= size - used) { out[used] = 0; full = true; continue; }
        used += size_t(n); ++count;
    }
    f_closedir(&dir);
    return result == FR_OK ? 0 : -250;
}
int InstrumentBackend::fileOpen(const char *path, uint32_t &bytes) {
    fileClose();
    if (!application_.fs_.mounted()) return -250;
    const bool shots = !std::strncmp(path, storage::Layout::Screenshots, std::strlen(storage::Layout::Screenshots));
    FILINFO info{};
    if (const FRESULT found = f_stat(path, &info); found != FR_OK || (info.fattrib & (AM_DIR | AM_HID | AM_SYS)))
        return found == FR_OK || found == FR_NO_FILE || found == FR_NO_PATH ? -256 : found == FR_INVALID_NAME ? -257 : -250; // As MMEM:CAT? lists.
    const bool screenshot = shots && screenshot::ImageCatalog::supportedName(info.fname);
    if (screenshot && application_.images_.clearing()) return -221;
    const FRESULT result = f_open(&file_, path, FA_READ);
    if (result != FR_OK) return result == FR_NO_FILE || result == FR_NO_PATH ? -256 : result == FR_INVALID_NAME ? -257 : -250;
    fileOpen_ = true;
    fileShot_ = screenshot;
    ++storage::openReaders; storage::screenshotReaders += fileShot_; bytes = uint32_t(f_size(&file_));
    return 0;
}
size_t InstrumentBackend::fileRead(uint32_t at, uint8_t *out, size_t size) {
    UINT n = 0;
    if (!fileOpen_ || (f_tell(&file_) != at && f_lseek(&file_, at) != FR_OK) || f_read(&file_, out, UINT(size), &n) != FR_OK) return 0;
    return n;
}
void InstrumentBackend::fileClose() { if (fileOpen_) { f_close(&file_); --storage::openReaders; storage::screenshotReaders -= fileShot_; } fileOpen_ = false; }
int InstrumentBackend::fileDelete(const char *path) {
    if (!application_.fs_.mounted()) return -250;
    const auto &a = application_;
    const bool shots = !std::strncmp(path, storage::Layout::Screenshots, std::strlen(storage::Layout::Screenshots));
    FILINFO info{};
    if (const FRESULT found = f_stat(path, &info); found != FR_OK || (info.fattrib & (AM_DIR | AM_HID | AM_SYS)))
        return found == FR_OK || found == FR_NO_FILE || found == FR_NO_PATH ? -256 : found == FR_INVALID_NAME ? -257 : -250;
    // A DMM session's file: none while the saved sessions are browsed (one may be read), nor the .TMP being written.
    const char *recording = a.data_.meterSession().active() ? a.data_.meterSession().path() : nullptr;
    const size_t length = recording ? std::strlen(recording) : 0;
    const bool written = length > 3 && std::strlen(path) == length && !strncasecmp(path, recording, length - 3);
    if (written || (a.sessionsBrowsing() && !strncasecmp(path, storage::Layout::Logs, std::strlen(storage::Layout::Logs)))) return -221;
    if (storage::openReaders || (shots && (a.imageReader_.busy() || a.images_.clearing() || a.panel_ == ApplicationController::Panel::Images ||
        (a.panel_ == ApplicationController::Panel::Confirmation && a.confirmationImages_)))) return -221; // Being read or browsed.
    const FRESULT result = f_unlink(path);
    return result == FR_OK ? 0 : result == FR_NO_FILE || result == FR_NO_PATH ? -256 : result == FR_INVALID_NAME ? -257 : -250;
}
bool InstrumentBackend::counter(unsigned channel,scope::CounterReading &reading) const {
    if (channel>1 || application_.data_.zeroBusy()) return false;
    reading=application_.data_.scopeCounters().channels[channel];
    return reading.valid;
}
scope::CaptureProgress InstrumentBackend::captureProgress() const {
    const auto &data=application_.data_;
    return data.measurementMode()==MeasurementMode::Oscilloscope && !data.zeroBusy() ? data.scopeProgress() : scope::CaptureProgress{};
}
bool InstrumentBackend::performance(PerformanceSnapshot &p) const {
    auto &a = application_;
    p = {Board::milliseconds(),SystemCoreClock,a.data_.scopeCaptures(),
        a.uiReady_ ? a.view_.waveform().model().statistics().projections : 0,
        a.display_.flushes(),a.performance_.elapsed(),a.performance_.ui()};
    return true;
}
bool InstrumentBackend::memory(MemorySnapshot &m) const { return application_.uiReady_ && GuiHeap::read(m); }
size_t InstrumentBackend::stalls(char *out, size_t size) const { return application_.stalls_.text(out, size); }
static_assert(BootTiming::Count * 11 + 6 < scpi::Backend::BootTextBytes, "SYST:BOOT? buffer");
size_t InstrumentBackend::bootTimes(char *out, size_t size) const { return BootTiming::text(out, size); }
size_t InstrumentBackend::meterPacket(const uint8_t *&data) const { return application_.data_.meter().packet(data); }
uint32_t InstrumentBackend::screenCapture() {
    auto &a = application_;
    const uint32_t now = Board::milliseconds();
    if (!a.uiReady_ || a.shots_.busy() || a.imageShowing_ || a.imageReader_.busy()) return 0; // Owned.
    const uint32_t ticket = a.screenLease_.take(now);
    if (!ticket) return 0; // Another stream's.
    lv_refr_now(nullptr);
    a.display_.settle(); // The frame just drawn is on screen, as for a screenshot.
    std::memcpy(screenshot::previewMemory(), a.display_.frame(), BmpImage::PixelBytes);
    return ticket;
}
void InstrumentBackend::screenEnd(uint32_t ticket) { application_.screenLease_.end(ticket); }
size_t InstrumentBackend::screenRead(uint32_t ticket, uint32_t offset, uint8_t *out, size_t size) {
    if (!application_.screenLease_.read(ticket, Board::milliseconds())) return 0; // Gone: the engine pads (-230).
    return BmpImage(screenshot::previewMemory()).read(offset, out, size);
}
bool InstrumentBackend::factoryCalibration(FactoryCalibration &f) const {
    const auto &record = application_.data_.scopeCalibration().factory();
    f = FactoryCalibration{};
    f.valid = record.valid(); f.identity = record.crc();
    for (unsigned c = 0; c < 2; ++c) { f.gainWord[c] = record.gainWord(c); f.scale[c] = record.factoryScale(c); }
    return true;
}
bool InstrumentBackend::factoryRecordFormat(FactoryRecordReading &f) const {
    const auto &record = application_.data_.scopeCalibration().factory();
    f.policy = factoryRecordName(application_.data_.factoryRecordPolicy());
    f.format = record.valid() ? factoryRecordName(record.format()) : "INVALID";
    return true;
}
const char *InstrumentBackend::version() const { return OPENOSC_VERSION; }
bool InstrumentBackend::frameKey(char *out, size_t capacity) const {
    if (!lease_.valid()) return false;
    const auto &k = leaseKey_;
    char time[24]; size_t n = 0; char digits[24]; uint64_t v = k.timestampUs; // nano printf: no %llu.
    do { digits[n++] = char('0' + v % 10); v /= 10; } while (v);
    for (size_t i = 0; i < n; ++i) time[i] = digits[n - 1 - i];
    time[n] = 0;
    std::snprintf(out, capacity, "%lu,%lu,%s,%lu,%lu,0x%08lX", (unsigned long)k.generation, (unsigned long)k.sequence, time,
                  (unsigned long)k.origin, (unsigned long)k.samples, (unsigned long)k.fingerprint); // Every field of RecordKey.
    return true;
}
// DECode: the decoder's state and published result (its full record key), and its CSV through the one
// export of the workspace: this session keeps only the ticket.
bool InstrumentBackend::decodeReport(scpi::DecodeReport &r) const {
    static constexpr const char *statuses[] = {"idle", "decoding", "done", "cancelled", "no_scale", "out_of_range", "channel_off",
                                               "unsupported", "timing", "ac_coupled"};
    static constexpr const char *protocols[] = {"OFF", "UART", "I2C", "SPI", "LIN", "CAN", "RS232", "RS485", "DMX512"};
    static_assert(std::size(statuses) == unsigned(decoding::DecodeStatus::Coupling) + 1);
    const auto protocol = [](decoding::Protocol p) { return protocols[unsigned(p) < std::size(protocols) ? unsigned(p) : 0]; };
    static_assert(std::size(protocols) == unsigned(decoding::Protocol::Count));
    const auto &d = application_.prefs_.scope.decoder;
    r.enabled = d.enabled;
    r.protocol = protocol(d.protocol);
    const auto &results = application_.decoder_.results();
    r.revision = results.revision();
    const auto *set = d.enabled ? results.published() : nullptr;
    r.result = set != nullptr;
    if (!set) return true;
    const auto &k = set->key();
    r.protocol = protocol(set->protocol());
    r.status = statuses[unsigned(set->status) < std::size(statuses) ? unsigned(set->status) : 0];
    r.events = set->count(); r.lost = set->lost(); r.dropped = set->dropped; r.overflows = set->overflows; r.clipped = set->clipped;
    r.processed = set->processed; r.samples = set->samples;
    r.generation = k.generation; r.sequence = k.sequence; r.timestampUs = k.timestampUs; r.origin = k.origin;
    r.fingerprint = k.fingerprint; r.settingsHash = set->settingsHash();
    r.fsNumerator = set->samplesPerSecond.numerator; r.fsDenominator = set->samplesPerSecond.denominator;
    return true;
}
uint32_t InstrumentBackend::decodeExport(int16_t &error) {
    auto &csv = decoding::workspaceExport();
    const uint32_t ticket = application_.prefs_.scope.decoder.enabled
        ? csv.begin(decoding::CsvExport::Owner::Usb, application_.decoder_.results(), application_.decoder_.settings(), "OpenOSC " OPENOSC_VERSION) : 0;
    if (!ticket) error = csv.refusal() == decoding::CsvExport::Refusal::Busy ? -200 : -230;
    return ticket;
}
int InstrumentBackend::decodeMeasure(uint32_t ticket, uint32_t &total) { return decoding::workspaceExport().measure(ticket, total); }
size_t InstrumentBackend::decodeRead(uint32_t ticket, uint8_t *output, size_t capacity) {
    return decoding::workspaceExport().read(ticket, output, capacity);
}
void InstrumentBackend::decodeEnd(uint32_t ticket) { decoding::workspaceExport().end(ticket); }
int16_t InstrumentBackend::decodeSave() { return application_.saveDecoderCsv(Board::milliseconds()); }
const char *InstrumentBackend::decodeSaved() const { return application_.decoderCsvSaved(); }
} // namespace openosc
