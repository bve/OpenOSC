#include "openosc/instrument/data_access.hpp"
#include "openosc/calibration/record.hpp"
#include "openosc/acquisition/sample_plan.hpp"
#include "openosc/generator/control_bits.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "openosc/platform/interrupts.hpp"
#include <algorithm>
#include <type_traits>

namespace openosc {
namespace {
// The ZT703S DAC stream announces its start; the ZT706S module (UART, no DMA) has nothing to announce.
#if OPENOSC_BOARD == 703
static_assert(std::is_same_v<board::GeneratorOutput, generator::Output>, "ZT703S: the DAC generator driver");
void attachBus(generator::Output &output, generator::BusClient &client) { output.attach(client); }
#else
static_assert(std::is_same_v<board::GeneratorOutput, generator::ModuleOutput>, "ZT706S: the module generator driver");
void attachBus(generator::ModuleOutput &, generator::BusClient &) {}
#endif
} // namespace
DataAccess::DataAccess(Filesystem &fs)
    : resources_(PeripheralResources::StorageDma), zeroFile_(fs),pwmFile_(fs,calibration_),gainFile_(fs,calibration_),interleaveFile_(fs,calibration_),zeroOperation_(*this), frontend_(scopeCalibration_), capture_(resources_, frontend_), counters_(resources_), uart_(resources_),
      generator_(resources_), serial_(resources_), recorder_(fs), logger_(fs) {
    attachBus(generator_, *this);
    StorageAdmission::attach(this); // NOR SPI transfers ask the capture to yield first.
}
bool DataAccess::initialize(MeasurementMode view, FactoryRecordPolicy factoryRecord) {
    CalibrationRecord record;
    factoryRecordPolicy_ = factoryRecord;
    record.load(calibration_, factoryRecord); // Refused: the record stays invalid and ScopeCalibration uses defaults.
    if (calibration_.valid()) {
        zeroFile_.load(calibration_.crc());
        if (pwmFile_.load()) scopeCalibration_.setPwm(pwmFile_.values());
        if (gainFile_.load()) scopeCalibration_.setGain(gainFile_.values());
        if (interleaveFile_.load()) scopeCalibration_.setInterleave(interleaveFile_.values());
#if OPENOSC_BOARD == 703
        interleaveMatchProfiles_.load(calibration_.crc(), reinterpret_cast<const uint32_t *>(UID_BASE));
#endif
    }
    previousTick_ = Board::milliseconds();
    timeUs_ = uint64_t(previousTick_) * 1000;
    if (!generator_.configure(generator::Settings{})) {
        status_ = DataStatus::HardwareError;
        return false;
    }
    generator_.disable(); // A known OFF state (the ZT706S module may still play what it had).
    initialized_ = true;
    return select(view == MeasurementMode::Generator ? MeasurementMode::Oscilloscope : view);
}
unsigned DataAccess::calibrationArchived() const {
    return (zeroFile_.repaired() ? unsigned(ZeroJson) : 0u) | (pwmFile_.repaired() ? unsigned(PwmJson) : 0u) |
           (gainFile_.repaired() ? unsigned(GainJson) : 0u) | (interleaveFile_.repaired() ? unsigned(InterleaveJson) : 0u);
}
bool DataAccess::select(MeasurementMode view) {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (zeroBusy()) return false;
    if (!initialized_ || stopping_ || unsigned(view) >= InstrumentModeCount)
        return false;
    if (view == MeasurementMode::Generator)
        return true;
    if (active_ && view == measurement_)
        return true;
    // Leaving the DMM: its commands are cancelled; a stroke whose 20 went sends its 21 first, and until
    // that has left the switch is refused (Busy), nothing stopped yet.
    if (!dmm_.close(Board::milliseconds())) { status_ = DataStatus::Busy; return false; }
    if (!serial_.stop()) { status_=DataStatus::HardwareError; return false; }
    if (uart_.txStatus() == DataStatus::Busy) {
        status_ = DataStatus::Busy;
        return false;
    }
    if (!stopCapture(true)) {
        status_ = DataStatus::HardwareError;
        return false;
    }
    if (!uart_.stop()) {
        status_ = DataStatus::HardwareError;
        return false;
    }
    counters_.stop();
    const auto previous = measurement_;
    if (previous == MeasurementMode::Multimeter && view != previous) watchdog_.left(Board::milliseconds());
    const bool restore = active_;
    active_ = false;
    if (activate(view)) {
        // The mode changed: leaving or entering the DMM starts a new ampere-hour count (GitHub #132). A
        // refused switch keeps it; a clock change or a resume (activate alone) only ends its run (expire).
        if (view != previous) meter_.resetCharge();
        return true;
    }
    const auto failure = status_;
    if (restore && previous != view) {
        if (!uart_.stop() || !stopCapture(true) || !activate(previous))
            status_ = DataStatus::HardwareError;
        else
            status_ = failure;
        if (previous == MeasurementMode::Multimeter) watchdog_.reselect(); // Its function, if it was power-cycled.
    }
    return false;
}
bool DataAccess::activate(MeasurementMode view) {
    active_ = false;
    meter_.cancelHold(); // A mode's cycle belongs to the DMM screen it was armed on.
    dmm_.abandon(); // select() let no stroke run into this (it refuses while one is owed).
    framer_.reset();
    captureFailed_ = false;
    measurement_ = view;
    if (view == MeasurementMode::Multimeter) meter_.expire(timeUs_); // A reading from before is not current.
    uint32_t remainingMs = 0;
    if (view != MeasurementMode::Multimeter) watchdog_.deactivate();
    else if (watchdog_.activate(Board::milliseconds(), timeUs_, remainingMs)) { // Powered on by serviceDmmWatchdog.
        frontend_.multimeterOff();
        dmmActivatedAtUs_ = timeUs_ + uint64_t(remainingMs) * 1000; // A selection waits for the power-on.
        active_ = true;
        status_ = DataStatus::NoData;
        return true;
    }
    if (view == MeasurementMode::Multimeter) {
        dmmActivatedAtUs_ = timeUs_;
        meterGeneratorBits_ = generator::controlBits(generator_.offConfirmed());
        frontend_.multimeter(meterGeneratorBits_);
        if (!uart_.start()) {
            status_ = uart_.status();
            return false;
        }
        receiveErrors_ = uart_.errors();
        receiveOverflows_ = uart_.overflows();
        active_ = true;
        status_ = DataStatus::NoData;
        return true;
    }
    return activateScope();
}
bool DataAccess::generatorLoadsBus() const {
    // The ZT703S DAC stream, also while it announces its start; the ZT706S module's UART never.
    return generator_.loadsBus() || busAnnounced_;
}
bool DataAccess::findTrigger(const scope::FrameView &frame,uint32_t &index) {
    if (frame.metadata().rolling) return false;
    if (frame.metadata().triggered) { index=frame.metadata().triggerIndex; return true; }
    return triggerCache_.find(frame,triggerForFrame(frame.metadata()),index);
}
bool DataAccess::acquireScope(scope::FrameLease &lease, bool consume) {
    if (zeroBusy() || zeroBlocked_) return false;
    if (!store_.acquireLatest(lease, consume))
        return false;
    if (lease.view().metadata().rolling) { lease.annotateTrigger(false,0); return true; }
    if (!scopeHeld_ && beforeArm(lease.view().metadata().sequence)) { // Normal too: a retained record is display only.
        lease.release(); return false;
    }
    const uint32_t sequence = lease.view().metadata().sequence;
    uint32_t trigger = 0;
    bool found = false;
    // The settings an index is found for now; a held record keeps those it was first shown with. A
    // capture's own edge (a gated record) is kept by the annotation itself.
    const auto &metadata = lease.view().metadata();
    scope::TriggerEdge edge = metadata.triggerEdge.known ? metadata.triggerEdge : triggerForFrame(metadata).tag();
    if (!scopeHeld_ || !heldTrigger_.recall(sequence, found, trigger, &edge)) {
        found = findTrigger(lease.view(), trigger);
        if (scopeHeld_) heldTrigger_.remember(sequence, found, trigger, edge);
    }
    lease.annotateTrigger(found, trigger, edge);
    if (!scopeHeld_ && !triggerAccepts(lease.view().metadata(), found)) {
        lease.release();
        return false;
    }
    return true;
}
bool DataAccess::acquireShownScope(scope::FrameLease &lease, const scope::FrameMetadata &shown) {
    if (zeroBusy() || zeroBlocked_ || shown.rolling || !store_.acquireLatest(lease, false)) return false;
    if (lease.view().metadata().sequence == shown.sequence &&
        lease.view().metadata().timestampUs == shown.timestampUs &&
        lease.annotateTrigger(shown.triggered, shown.triggerIndex, shown.triggerEdge)) return true;
    lease.release();
    return false;
}
const scope::FrameView *DataAccess::readoutFrame(uint32_t sequence) {
    if (readout_.valid() && readout_.view().metadata().sequence == sequence) return &readout_.view();
    releaseReadout();
    scope::FrameLease lease;
    if (!acquireScope(lease) || lease.view().metadata().sequence != sequence) return nullptr;
    readout_ = std::move(lease);
    return &readout_.view();
}
void DataAccess::releaseReadout() {
    if (!readout_.valid()) return;
    readout_.release();
    ++readoutRevision_;
}
bool DataAccess::acquireScopeWait(scope::FrameLease &lease,uint32_t timeoutMs) {
    const uint32_t start=Board::milliseconds();
    // Frames behind an overlay are old, so a paused capture has to produce a new one first.
    const bool stale=capturePaused_ && !scopeHeld_;
    const uint32_t firstFresh=sequence_+1;
    noteFrameDemand(start);
    // Re-check HOLD each pass: a Single that completes during the wait is the frame to return.
    while (!acquireScope(lease) || (stale && !scopeHeld_ && int32_t(lease.view().metadata().sequence-firstFresh)<0)) {
        lease.release();
        const uint32_t now=Board::milliseconds();
        if (uint32_t(now-start)>=timeoutMs) return false;
        poll(now);
        Board::service();
    }
    return true;
}
bool DataAccess::sendDmmGroup(dmm::CommandGroup group) {
    if (unsigned(group) >= 5 || !active_ || measurement_ != MeasurementMode::Multimeter || watchdog_.off() ||
        !dmm_.pressGroup(group, meter_.latest(), Board::milliseconds(), dmmActivatedAtUs_)) return false;
    meter_.typeK(false, timeUs_);
    meter_.reselect(); // No function is awaited: the packets show which one the key gave.
    return true;
}
bool DataAccess::selectDmmFunction(dmm::Function function) { // Also while off: it waits for packets.
    // Held off (dmm::LinkWatchdog::OffMs): its deadline runs from the power-on.
    const uint32_t waitMs = dmmActivatedAtUs_ > timeUs_ ? uint32_t(std::min<uint64_t>((dmmActivatedAtUs_ - timeUs_) / 1000, dmm::FunctionSelector::PowerOnWaitMs)) : 0;
    const bool typeK = function == dmm::Function::Temperature;
    const auto physical = typeK ? dmm::Function::MillivoltsDc : function;
    if (!active_ || measurement_ != MeasurementMode::Multimeter ||
        (typeK && !meter_.temperatureSettings().configured()) ||
        !dmm_.selectFunction(physical, meter_.latest(), Board::milliseconds() + waitMs, dmmActivatedAtUs_)) return false;
    meter_.typeK(typeK, timeUs_);
    // Accepted (a refusal above changes nothing): the statistics period starts with this function.
    meter_.restart(function, timeUs_ + (uint64_t(dmm::FunctionSelector::DeadlineMs) + waitMs) * 1000);
    return true;
}
bool DataAccess::pressDmmRange() {
    if (!active_ || measurement_ != MeasurementMode::Multimeter || watchdog_.off() || meter_.typeK() ||
        !dmm_.pressRange(meter_.latest(), Board::milliseconds(), timeUs_, dmmActivatedAtUs_)) return false;
    meter_.rangeCommand();
    return true;
}
bool DataAccess::releaseDmmRange(bool sendRelease) { return dmm_.releaseRange(sendRelease, Board::milliseconds()); }
bool DataAccess::holdDmmRange() { return dmm_.holdRange(Board::milliseconds()); }
void DataAccess::closeDmmRange() { if (dmm_.rangeOpen()) dmm_.close(Board::milliseconds()); }
bool DataAccess::requestDmmKey(dmm::KeyCommand::Action action) {
    if (!active_ || measurement_ != MeasurementMode::Multimeter || watchdog_.off() ||
        !dmm_.requestKey(action, meter_.latest(), Board::milliseconds(), dmmActivatedAtUs_)) return false;
    using Action = dmm::KeyCommand::Action;
    if (action == Action::NextRange || action == Action::Auto || action == Action::Manual) meter_.rangeCommand();
    return true;
}
bool DataAccess::setDmmAuto(bool automatic) {
    return !meter_.typeK() && requestDmmKey(automatic ? dmm::KeyCommand::Action::Auto : dmm::KeyCommand::Action::Manual);
}
bool DataAccess::holdMeter(bool held) {
    if (!active_ || measurement_ != MeasurementMode::Multimeter) return false;
    if (!held && meter_.latest().packetHold && !requestDmmKey(dmm::KeyCommand::Action::ReleaseHold)) return false;
    if (!held) meter_.cancelHold();
    return meter_.hold(held);
}
bool DataAccess::armMeterHold() {
    if (!active_ || measurement_ != MeasurementMode::Multimeter || meter_.userHeld()) return false;
    return meter_.armHold(timeUs_);
}
bool DataAccess::relativeMeter(bool enabled) {
    if (!active_ || measurement_ != MeasurementMode::Multimeter) return false;
    if (meter_.latest().peerRelative) {
        if (enabled) return true;
        if (!requestDmmKey(dmm::KeyCommand::Action::RelativeOff)) return false;
    }
    if (enabled && meter_.status(timeUs_, 2000000) == DataStatus::Stale) return false;
    return meter_.relative(enabled);
}
bool DataAccess::setDmmRange(unsigned index) {
    if (!active_ || measurement_ != MeasurementMode::Multimeter || watchdog_.off() || meter_.typeK() ||
        !dmm_.setRange(index, meter_.latest(), Board::milliseconds(), dmmActivatedAtUs_)) return false;
    meter_.rangeCommand(); // A new scale: a hold mode's window starts again.
    return true;
}
bool DataAccess::stepDmmRange(int direction) {
    unsigned index, count;
    const auto &result = meter_.latest();
    if (!dmm::Ranges::current(result, &index)) return false;
    dmm::Ranges::list(dmm::Functions::identify(result), count);
    if (direction > 0 && index + 1 < count) ++index;
    if (direction < 0 && index) --index;
    return setDmmRange(index);
}
bool DataAccess::recordScope(ScopeRecorder::Format format) {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (zeroBusy()) return false;
    if (format != ScopeRecorder::Format::Raw && format != ScopeRecorder::Format::Csv)
        return false;
    if (recordPending_ || recorder_.busy() || (!scopeHeld_ && measurement_ != MeasurementMode::Oscilloscope))
        return false;
    recordPending_ = true;
    recordDeadlineUs_ = timeUs_ + 2000000;
    recordStatus_ = DataStatus::Busy;
    recordFormat_ = format;
    return true;
}
void DataAccess::beginShutdown() {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (stopping_)
        return;
    if (const auto *original=zeroOperation_.abandon()) {
        request_=*original; scopeHeld_=false; zeroBlocked_=true; zeroMinimumSequence_=sequence_+1;
    }
    stopping_ = true;
    // A stroke owing its 21 (within the factory's window) sends it before the UART stops; a pair on
    // its way is waited for (serviceDmmClose, bounded); later nothing is owed.
    dmmClosing_ = !dmm_.close(Board::milliseconds());
    dmmCloseDeadline_ = Board::milliseconds() + DmmCloseMs;
    generator_.disable();
    serial_.stop();
    counters_.stop();
    stopCapture(true);
    if (!dmmClosing_) stopDmmUart();
    heldFrame_.release();
    recordPending_ = false;
    logger_.finish();
}
void DataAccess::serviceDmmClose(uint32_t now) {
    dmm_.service(uart_, meter_.latest(), now);
    if (!dmm_.owes() || int32_t(now - dmmCloseDeadline_) >= 0) stopDmmUart();
}
void DataAccess::stopDmmUart() {
    dmm_.abandon(); dmmClosing_ = false;
    uart_.stop();
}
bool DataAccess::resume() {
    if (!stopping_) return true;
    if (dmmClosing_) stopDmmUart(); // activate() starts the UART again.
    stopping_ = false;
    return activate(measurement_);
}
} // namespace openosc
