#include "openosc/board/current.hpp"
#include "openosc/application/boot_timing.hpp"
#include "openosc/application/scpi_backend.hpp"
#include "openosc/screenshot/reader.hpp"
#include <cstring>
#include "openosc/application/controller.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/clocks.hpp"
#include <cstdio>

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
bool InstrumentBackend::configure(const ScopeRequest &r, bool level) {
    if (application_.data_.zeroBusy()) return false;
    auto &a = application_; if (!a.cancelAuto(Board::milliseconds())) return false;
    return a.applyFollowing(r,Board::milliseconds(),ApplicationController::ScopeApply::StageIfHeld,
                            level ? ApplicationController::LevelIntent::Set : ApplicationController::LevelIntent::Follow);
}
bool InstrumentBackend::configureScope(const ScopeRequest &r) { return configure(r, false); }
bool InstrumentBackend::setTrigger(const ScopeRequest &r) { return configure(r, true); }
void InstrumentBackend::demand() const { application_.data_.frameHosts().demand(host_, Board::milliseconds()); }
bool InstrumentBackend::run(bool single) {
    if (application_.data_.zeroBusy()) return false;
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
        a.data_.scopeRecording() || a.autoBusy()) return false;
    a.startAuto(Board::milliseconds());
    return a.autoBusy();
}
// TFORce: the acquisition's forced record; AutoSetup trials are its own refusal, zero/HOLD/Single
// done are DataAccess's.
bool InstrumentBackend::forceTrigger() {
    auto &a = application_;
    if (a.data_.measurementMode() != MeasurementMode::Oscilloscope || a.autoBusy()) return false;
    demand(); // The forced record is read next (Root HIL 2026-11-13: Normal TFOR lost it to the next capture).
    return a.data_.forceScope();
}
uint32_t InstrumentBackend::frameSequence() const {
    const auto &a = application_;
    const auto &d = a.data_;
    if (d.measurementMode() != MeasurementMode::Oscilloscope || d.zeroBusy()) return 0;
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
    if (!a.cancelAuto(Board::milliseconds(),true)) return;
    a.data_.holdScope(true); a.dataRefresh_=true;
}
bool InstrumentBackend::held() const { return application_.data_.scopeHeld(); }
bool InstrumentBackend::waiting() const { return application_.data_.scopeWaiting(); }
bool InstrumentBackend::triggered() const { return application_.data_.scopeTriggered(); }
const scope::FrameView *InstrumentBackend::frame(bool refresh) {
    if (refresh) releaseFrame();
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
bool InstrumentBackend::busy() const {
    return application_.data_.dmmBusy() || application_.meterRestorePending() || application_.data_.zeroBusy() || application_.autoBusy();
}
uint32_t InstrumentBackend::operationTimeoutMs() const {
    if (application_.autoBusy()) return 602000;
    if (application_.data_.zeroBusy())
        // The procedure's own deadline, then the restore (12 s) and a new capture of the original sweep.
        return application_.data_.zeroOperation().calibration().durationMs()+14000+
               uint32_t(application_.data_.zeroOperation().calibration().original().timebaseNs/100000);
    if (application_.meterRestorePending()) return MeterRestore::ChainMs + 2000; // Selection + restore, and a packet.
    return 10000;
}
DataStatus InstrumentBackend::operationStatus() const { return application_.data_.dmmCommandStatus(); }
generator::Snapshot InstrumentBackend::generator() const { return application_.data_.generator().snapshot(); }
bool InstrumentBackend::configureGenerator(const generator::Settings &s) { return application_.applyGenerator(s,Board::milliseconds()); }
bool InstrumentBackend::output(bool value) { return application_.requestGeneratorOutput(value, Board::milliseconds()); }
scpi::Backend::InterleaveStatus InstrumentBackend::interleave() const {
    const auto &a = application_;
    const uint32_t blockers = a.data_.interleaveBlockers();
    const auto &features = board::Current::Features;
    const auto mode = scope::InterleaveMode(a.prefs_.scope.interleave);
    return {features.interleaveControl(), mode == scope::InterleaveMode::On, !blockers, blockers,
            features.adcInterleave, mode == scope::InterleaveMode::Auto};
}
bool InstrumentBackend::interleave(bool on) {
    return application_.selectInterleave(on ? scope::InterleaveMode::On : scope::InterleaveMode::Off, Board::milliseconds());
}
bool InstrumentBackend::interleaveAuto() { return application_.selectInterleave(scope::InterleaveMode::Auto, Board::milliseconds()); }
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
    switch (application_.changeRunMode(mode, Board::milliseconds(), RemoteClock::persists(application_.remote_)).outcome) {
    case RunModeSwitch::Outcome::Applied:
    case RunModeSwitch::Outcome::Unchanged: return 0;
    case RunModeSwitch::Outcome::Refused: return -221;    // Busy: nothing changed.
    default: return -200;                                  // Not applied, or users not restarted: query says what runs.
    }
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
                                               "unsupported", "timing"};
    static constexpr const char *protocols[] = {"OFF", "UART", "I2C", "SPI", "LIN", "CAN"};
    const auto &d = application_.prefs_.scope.decoder;
    r.enabled = d.enabled;
    r.protocol = protocols[unsigned(d.protocol) < 6 ? unsigned(d.protocol) : 0];
    const auto &results = application_.decoder_.results();
    r.revision = results.revision();
    const auto *set = d.enabled ? results.published() : nullptr;
    r.result = set != nullptr;
    if (!set) return true;
    const auto &k = set->key();
    r.protocol = protocols[unsigned(set->protocol()) < 6 ? unsigned(set->protocol()) : 0];
    r.status = statuses[unsigned(set->status) < 9 ? unsigned(set->status) : 0];
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
