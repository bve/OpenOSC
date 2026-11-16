#include "openosc/application/scpi_backend.hpp"
#include "openosc/application/controller.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/clocks.hpp"

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
bool InstrumentBackend::configureScope(const ScopeRequest &r) {
    if (application_.data_.zeroBusy()) return false;
    auto &a = application_; if (!a.cancelAuto(Board::milliseconds())) return false;
    return a.applyFollowing(r,Board::milliseconds(),ApplicationController::ScopeApply::StageIfHeld);
}
void InstrumentBackend::demand() const { application_.data_.frameHosts().demand(host_, Board::milliseconds()); }
bool InstrumentBackend::run(bool single) {
    if (application_.data_.zeroBusy()) return false;
    application_.data_.frameHosts().rearm(host_, application_.data_.scopeFrameSequence(), Board::milliseconds());
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
    const auto &d = application_.data_;
    if (d.measurementMode() != MeasurementMode::Oscilloscope || d.zeroBusy()) return 0;
    demand(); // ACQ:FRAME?: the host polls for the record it will read.
    return d.scopeFrameSequence();
}
void InstrumentBackend::stop() {
    auto &a=application_;
    if (!a.cancelAuto(Board::milliseconds(),true)) return;
    a.data_.holdScope(true); a.dataRefresh_=true;
}
bool InstrumentBackend::held() const { return application_.data_.scopeHeld(); }
bool InstrumentBackend::waiting() const { return application_.data_.scopeWaiting(); }
const scope::FrameView *InstrumentBackend::frame(bool refresh) {
    if (refresh) releaseFrame();
    application_.data_.noteFrameDemand(Board::milliseconds());
    demand();
    if (!lease_.valid() && !application_.data_.acquireScope(lease_)) return nullptr;
    const auto &m = lease_.view().metadata();
    application_.data_.frameHosts().taken(host_, m.sequence); // Leased: its memory is held by the lease now.
    if (freshFrame_ && !application_.data_.scopeHeld() && m.sequence == previousFrameSequence_ && m.timestampUs == previousFrameTime_) {
        lease_.release(); return nullptr; // Leave the store writable until a newer DMA frame is published.
    }
    freshFrame_ = false;
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
bool InstrumentBackend::factoryCalibration(FactoryCalibration &f) const {
    const auto &factory = application_.data_.scopeCalibration().factory();
    f = {factory.valid(), factory.crc(), {factory.gainWord(0), factory.gainWord(1)}};
    return true;
}
const char *InstrumentBackend::version() const { return OPENOSC_VERSION; }
} // namespace openosc
