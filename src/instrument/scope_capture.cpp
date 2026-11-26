#include "openosc/instrument/data_access.hpp"
#include "openosc/calibration/channel_scale.hpp"
#include "openosc/acquisition/interleave.hpp"
#include "openosc/board/current.hpp"
#include "openosc/acquisition/sample_plan.hpp"
#include "openosc/platform/board.hpp"
#include "openosc/platform/usb_trace.hpp"
#include "openosc/platform/interrupts.hpp"
#include <algorithm>
#include <utility>

// The scope acquisition lifecycle: requests and their plan, the record memory each capture takes,
// publication of finished records, HOLD, and retry after a capture fault.
namespace openosc {
namespace {
bool sameCh1Range(const ScopeRequest &a, const ScopeRequest &b) {
    return a.frontend.range[0] == b.frontend.range[0] &&
           a.frontend.position[0] == b.frontend.position[0] &&
           a.frontend.coupling[0] == b.frontend.coupling[0];
}
}
bool DataAccess::validateScope(const ScopeRequest &r) const {
    if (r.timebaseNs < 10 || r.timebaseNs > 20000000000ULL || !scope::RecordBudget::valid(r.recordPoints) ||
        !r.trigger.valid())
        return false;
    for (unsigned c = 0; c < 2; ++c)
        if (!r.probeNumerator[c] || !r.probeDenominator[c] || r.frontend.range[c] > 10 ||
            r.frontend.position[c] < 0 || r.frontend.position[c] > 160 ||
            unsigned(r.frontend.coupling[c]) > 1)
            return false;
    return r.frontend.enabled[0] || r.frontend.enabled[1];
}
bool DataAccess::configureScope(const ScopeRequest &r) {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (zeroBusy()) return false;
    return configureScopeCore(r);
}
bool DataAccess::stageScope(const ScopeRequest &r) {
    if (!scopeHeld_ || zeroBusy() || recorder_.busy() || !validateScope(r)) return false;
    const auto previous = request_;
    request_ = r;
    // configuration_ becomes the plan of the next RUN; the held frame keeps its own metadata.
    if (!describeNext(configuration_)) { request_ = previous; describeNext(configuration_); return false; }
    if (!sameCh1Range(previous, r) || !interleaving(r)) interleaveHeadroom_ = false;
    staged_ = true;
    return true;
}
bool DataAccess::retainsShown(const ScopeRequest &next) const {
    using Arena = scope::AcquisitionArena;
    auto trigger = next; trigger.trigger = request_.trigger; // Everything but the trigger stays.
    return next.trigger.mode != scope::TriggerMode::Auto && request_.trigger.mode != scope::TriggerMode::Single &&
           trigger.same(request_) && !request_.rolling() && !internalCapture() && store_.hasFrame() &&
           !store_.busy() && (store_.region() & Arena::Slots) && Arena::slots(request_.recordPoints, recordGeometry()) == 2;
}
bool DataAccess::configureScopeCore(const ScopeRequest &r,bool force,bool preserveRecord) {
    // The same request under other offsets (a hardware zero, a calibration trial) is a change too.
    if (!force && request_.same(r) && !captureFailed_ && frontendGeneration_==scopeCalibration_.generation()) {
        usbtrace::mark(usbtrace::Tag::ConfigBegin, 1); return true;
    }
    usbtrace::mark(usbtrace::Tag::ConfigBegin, 0);
    if (scopeHeld_ || recorder_.busy()) return false; // A busy owner refuses the change; capture stays healthy.
    if (!validateScope(r)) {
        status_ = DataStatus::InvalidArgument;
        return false;
    }
    const bool retain = !force && !preserveRecord && retainsShown(r);
    if (!stopCapture(false) || (!preserveRecord && !retain && !store_.cancel())) {
        status_ = DataStatus::Busy;
        return false;
    }
    // An admitted change ends a running interleave offset measurement here (its records would mix
    // conditions); the new capture takes the profile in force. An identical restart (the measurement's
    // own) keeps it. No reconfigure from here: this one is it.
    if (interleaveMeasure_.active() && (!request_.same(r) || !interleaveMeasureHolds(r)))
        interleaveMeasure_.end(calibration::InterleaveOffsetMeasurement::Result::Interrupted);
    const auto previous = request_;
    const bool previousHeadroom = interleaveHeadroom_;
    if (!sameCh1Range(previous, r) || !interleaving(r)) interleaveHeadroom_ = false;
    request_ = r;
    forcePending_ = false; forcedSequence_ = 0; // A force belongs to the acquisition it was given for.
    if (!describeNext(configuration_)) describeChannels(configuration_); // Conversions follow the request.
    // In a burst of setting changes only the last one starts a capture (poll waits for quiet): each
    // start reconfigures the capture hardware. HIL traces have seen USB33RDY low and USB resets
    // close to capture starts (2026-11-10); a cause is not established, fewer starts is the hedge.
    const uint32_t configuredAt = Board::milliseconds();
    scope::SamplePlan plan;
    const bool burst = uint32_t(configuredAt - configuredAt_) < ConfigQuietMs && samplePlan(plan);
    configuredAt_ = configuredAt;
    armSequence_ = sequence_ + 1;
    triggerWaiting_ = r.trigger.mode != scope::TriggerMode::Auto; acceptedTriggered_ = false;
    captureFailed_ = false;
    if (retain) store_.consume(store_.sequence()); // Seen: never a reason for the capture to wait.
    if (active_ && measurement_ == MeasurementMode::Oscilloscope && !burst && !startCapture() && !captureWaiting_) {
        const auto failure = status_;
        request_ = previous;
        interleaveHeadroom_ = previousHeadroom;
        if (!describeNext(configuration_)) describeChannels(configuration_);
        captureFailed_ = false;
        // A HOLD resume puts its record back instead: restarting the old capture would write over it.
        if (!preserveRecord && startCapture())
            status_ = failure;
        return false;
    }
    counters_.invalidate(Board::milliseconds());
    frontendGeneration_=scopeCalibration_.generation();
    usbtrace::mark(usbtrace::Tag::ConfigEnd);
    return true;
}
// The zero calibration's configure. Its front end is applied by the capture start, at once or
// deferred (a burst, memory still held), always as an analog change (forceChange): even settings
// the hardware already has get a settle origin after this configure, so no frame of an earlier
// capture or settle counts.
bool DataAccess::zeroConfigure(const ScopeRequest &request) {
    frontend_.forceChange();
    zeroApplies_=frontend_.applies();
    return configureScopeCore(request,true);
}
bool DataAccess::zeroAppliedAt(uint32_t &at) const {
    if (frontend_.applies()==zeroApplies_) return false;
    at=frontend_.changedAt(); return true;
}
bool DataAccess::internalCapture() const {
    // AutoSetup trials and zero calibration measure in the trial area, never over a record.
    return trials_ || zeroTrial();
}
// A zero calibration's own trial (Applying, Collecting): a finite record that fills the trial area,
// without a trigger. Its restoration (Restoring, Resuming) captures the original request into the
// record memory like any RUN, gated by its trigger: CaptureDma takes a capture without one only as a
// finite record of its whole single-segment, non-split ring (Balanced, Compact and Split rings are not).
bool DataAccess::zeroTrial() const {
    const auto phase=zeroOperation_.phase();
    return phase==calibration::ZeroOperation::Phase::Applying || phase==calibration::ZeroOperation::Phase::Collecting;
}
// The plan of the next capture: a record of the request's per-channel depth, the fixed trial
// depth, or a Roll window (at most RollWindow points per channel, as Roll keeps 16-bit history).
// A buffer the DMA cannot fill at the full rate (AcquisitionArena::fullRate) is planned at half
// of it; a ring shorter than the depth (the compact 128K one, a split ring's capacity) keeps the record
// within it, less the headroom at the full rate.
uint32_t DataAccess::captureRateLimit() const {
    const uint32_t timer = TimerClock::apb1();
    return timer / adcDividerAt(timer) / (generatorLoadsBus() ? 2U : 1U);
}
// The limit is in DMA transfers. The paired layout needs full-rate memory, carries two CH1 points
// per transfer and expresses its plan and headroom in points, in groups of eight.
bool DataAccess::samplePlan(scope::SamplePlan &plan, const scope::CaptureBuffer &buffer, bool rolling) const {
    const uint32_t perTransfer=buffer.pointsPerTransfer();
    const bool full=scope::AcquisitionArena::fullRate(buffer);
    if (perTransfer>1 && !full) return false;
    const uint32_t maximumRate=captureRateLimit()/(full ? 1U : 2U);
    const uint32_t capacity=rolling ? scope::AcquisitionArena::RollWindow : buffer.capacityPoints();
    const uint32_t depth = internalCapture() ? buffer.ringPoints()
        : rolling ? std::min<uint32_t>(request_.depth(), scope::AcquisitionArena::RollWindow)
        : std::min<uint32_t>(request_.depth(), capacity & ~(4*perTransfer-1));
    return scope::SamplePlan::calculate(request_.timebaseNs,depth,maximumRate,capacity,plan,
                                        rolling ? scope::SamplePlan::FullSpeedHeadroom
                                                : scope::SamplePlan::headroom(buffer,maximumRate*perTransfer),
                                        perTransfer, rolling || internalCapture() ? 0 : request_.windowPoints());
}
// What keeps a request from interleaving on this build and clock (InterleaveRule): the ZT706S's
// factory rule, or the ZT703S's selectable one with the user's choice (in a probe build, the probe's
// mode in place of it while one is set).
scope::InterleaveConditions::Inputs DataAccess::interleaveInputs(const ScopeRequest &r) const {
    return {TimerClock::apb1(), r.timebaseNs, r.frontend.enabled[0], r.frontend.enabled[1], r.rolling(), internalCapture(),
            generatorLoadsBus()};
}
bool DataAccess::interleaveSelected() const {
#ifdef OPENOSC_INTERLEAVE_PROBE
    if (probe_.active()) return probe_.mode() == scope::ProbeMode::Interleave;
#endif
    return interleaveMode_ == scope::InterleaveMode::On;
}
uint32_t DataAccess::interleaveBlockers(const ScopeRequest &r) const {
    // On (or the probe's Interleave), Auto by the board's policy (ZT706S factory, ZT703S MKR), else Off.
    bool probing = false;
#ifdef OPENOSC_INTERLEAVE_PROBE
    probing = probe_.active();
#endif
    const auto mode = interleaveSelected() ? scope::InterleaveMode::On
        : !probing && interleaveMode_ == scope::InterleaveMode::Auto ? scope::InterleaveMode::Auto : scope::InterleaveMode::Off;
    return scope::InterleaveRule::blockers(board::Current::Features, mode, interleaveInputs(r));
}
bool DataAccess::interleaving(const ScopeRequest &r) const { return !interleaveBlockers(r); }
// The choice (Settings > Device > Performance > Interleave CH1) takes effect as any setting
// does: an ordinary forced reconfigure of the same request, or in HOLD the Next plan (the held record
// keeps its own). Refused during a calibration, an AutoSetup or a recording; a refused reconfigure
// puts the previous choice back. Auto: the ZT706S's factory rule, the ZT703S's MKR one.
bool DataAccess::selectInterleave(scope::InterleaveMode mode) {
    using Mode = scope::InterleaveMode;
    if (mode > Mode::Auto) return false;
    if (!board::Current::Features.interleaveControl()) return mode == Mode::Off;
    if (mode == interleaveMode_) return true;
    if (zeroBusy() || internalCapture() || recorder_.busy()) return false;
    const auto previous = interleaveMode_;
    const bool previousHeadroom = interleaveHeadroom_;
    interleaveMode_ = mode;
    if (!interleaving(request_)) interleaveHeadroom_ = false;
    if (scopeHeld_ ? describeNext(configuration_) : configureScopeCore(request_, true)) return true;
    interleaveMode_ = previous;
    interleaveHeadroom_ = previousHeadroom;
    if (!scopeHeld_) configureScopeCore(request_, true);
    return false;
}
// The HIGH ADC's offset a new interleaved record takes, frozen in its metadata: the ZT706S's service
// or factory correction; the ZT703S's measured one of the clock that runs (none of the factory's: its
// record holds an in-phase one), 0 where none was measured and while one is being measured.
float DataAccess::recordInterleaveOffset() const {
    if (factoryInterleave()) return interleaveOffset();
    if (interleaveMeasure_.active()) return 0.f;
    const auto *clocked = scopeCalibration_.interleave().at(TimerClock::apb1(), adcDividerAt(TimerClock::apb1()));
    return clocked ? clocked->offset : 0.f;
}
const float *DataAccess::interleaveOffsetAtClock() const {
    const auto *clocked = scopeCalibration_.interleave().at(TimerClock::apb1(), adcDividerAt(TimerClock::apb1()));
    return clocked ? &clocked->offset : nullptr;
}
bool DataAccess::interleaveCalibrated() const {
    return factoryInterleave() || interleaveOffsetAtClock() != nullptr;
}
// The ZT703S interleave offset (InterleaveOffsetMeasurement), an asynchronous bounded operation:
// begun only where a finite record would interleave now (the choice on, CH1 alone, Auto, not
// HOLD, no calibration, AutoSetup or recording); the capture restarts without the correction; each
// fresh record is taken once as serviceScopeFrames inspects it; the result is saved for the clock that
// runs and the capture restarts with it. A failure, timeout, cancel or a change of the conditions ends
// it with the profile in force unchanged; HOLD and records already captured are never touched.
DataStatus DataAccess::beginInterleaveOffset(uint32_t now) {
    if (!board::Current::Features.selectableInterleave) return DataStatus::Unsupported;
    if (!calibration_.valid()) return DataStatus::Uncalibrated; // The saved offset needs a factory CRC.
    if (interleaveMeasure_.active() || zeroBusy() || internalCapture() || recorder_.busy() || scopeHeld_ ||
        !active_ || measurement_ != MeasurementMode::Oscilloscope) return DataStatus::Busy;
    if (!interleaving(request_) || request_.rolling() || request_.trigger.mode != scope::TriggerMode::Auto)
        return DataStatus::InvalidArgument;
    interleaveMeasure_.begin(now, TimerClock::apb1(), sequence_ + 1);
    interleaveGeneration_ = scopeCalibration_.generation();
    if (configureScopeCore(request_, true)) return DataStatus::Ok;
    interleaveMeasure_.end(calibration::InterleaveOffsetMeasurement::Result::Interrupted);
    configureScopeCore(request_, true);
    return DataStatus::Busy;
}
void DataAccess::cancelInterleaveOffset() { endInterleaveOffset(calibration::InterleaveOffsetMeasurement::Result::Cancelled); }
void DataAccess::endInterleaveOffset(calibration::InterleaveOffsetMeasurement::Result result) {
    if (!interleaveMeasure_.active()) return;
    interleaveMeasure_.end(result);
    if (!scopeHeld_) configureScopeCore(request_, true); // Records with the correction in force again.
}
// The conditions it was begun under: not held, the same clock, the request still interleaving (the
// choice, CH1 alone), Auto, Scope, and the front-end offsets (zero, PWM) it began under.
bool DataAccess::interleaveMeasureHolds(const ScopeRequest &r) const {
    return !scopeHeld_ && TimerClock::apb1() == interleaveMeasure_.coreHz() && interleaving(r) &&
           r.trigger.mode == scope::TriggerMode::Auto && measurement_ == MeasurementMode::Oscilloscope &&
           interleaveGeneration_ == scopeCalibration_.generation();
}
void DataAccess::serviceInterleaveOffset(uint32_t now) {
    using Result = calibration::InterleaveOffsetMeasurement::Result;
    // A finished measurement restarts the capture here, outside the inspection that ended it.
    if (interleaveRestart_) { interleaveRestart_ = false; if (!scopeHeld_) configureScopeCore(request_, true); }
    if (!interleaveMeasure_.active()) return;
    if (!interleaveMeasureHolds(request_)) endInterleaveOffset(Result::Interrupted);
    else if (interleaveMeasure_.expired(now)) endInterleaveOffset(Result::Timeout);
}
void DataAccess::finishInterleaveOffset() {
    using Result = calibration::InterleaveOffsetMeasurement::Result;
    if (interleaveMeasure_.result() == Result::Measured) {
        auto profile = scopeCalibration_.interleave(); // The ZT706S fields and other clocks kept.
        profile.factoryCrc = calibration_.crc();
        // This timing's entry; one of a timing no run mode has any more may give its place.
        const auto current = [](uint32_t coreHz, uint32_t divider) {
            for (const auto &mode : RunModes) if (mode.coreHz == coreHz && mode.adcDivider == divider) return true;
            return false;
        };
        if (!profile.measured(interleaveMeasure_.coreHz(), adcDividerAt(interleaveMeasure_.coreHz()), interleaveMeasure_.offset(), current) ||
            !saveInterleaveProfile(profile) ||
            !scopeCalibration_.setInterleave(profile))
            interleaveMeasure_.end(Result::SaveFailed);
    }
    interleaveRestart_ = true; // The next pass: records with the correction in force (serviceInterleaveOffset).
}
// What the analog front end is set to for a request: its settings, interleaved when it interleaves,
// with the ZT706S's CH1 range correction (InterleaveTracker) while it does. The request never changes.
scope::FrontendSettings DataAccess::appliedFrontend(const ScopeRequest &r) const {
    auto settings=r.frontend;
    settings.interleave=interleaving(r);
    settings.rangeBoost=settings.interleave && !r.rolling() && (factoryInterleave() ? interleave_.boost()
        : interleaveHeadroom_ && !interleaveMeasure_.active());
#ifdef OPENOSC_INTERLEAVE_PROBE
    settings.sharedInput=probe_.active() && probe_.applied(probeInputs(r)) == scope::ProbeMode::SharedInput;
#endif
    return settings;
}
// Every record captured interleaved, once: CH1's frequency over its first InterleaveTracker::MaximumPoints
// (the record's own analysis, bounded) for the range correction and display gain of the next capture.
void DataAccess::observeInterleave(const scope::FrameView &frame) {
    const auto &m=frame.metadata();
    if (m.layout != scope::Layout::InterleavedCh1 || m.rolling) return;
    if (!factoryInterleave()) {
        // Repeated rails in the acquired codes indicate a lost signal peak. Take the next
        // calibrated hardware range for new records while keeping the selected V/div on screen.
        if (interleaveHeadroom_ || interleaveMeasure_.active() || !m.channels[0].calibrated || request_.frontend.range[0] >= 10)
            return;
#if OPENOSC_BOARD == 703
        if (matchCalibrating_) return; // Keep the physical range fixed across calibration tones.
#endif
#ifdef OPENOSC_INTERLEAVE_PROBE
        if (probe_.active()) return;
#endif
        constexpr uint32_t Windows = 8, Window = 128, RailCount = 32;
        if (m.samples < Windows * Window) return;
        const uint32_t stride = (m.samples - Window) / (Windows - 1);
        uint32_t rails = 0;
        // Sample across the record: beating between the signal and ADC can concentrate clipping
        // far from the trigger even in a record with many clipped points.
        for (uint32_t i = 0; i < Windows && rails < RailCount; ++i)
            rails += frame.interleaveRails(i * stride, Window, RailCount - rails);
        interleaveHeadroom_ = rails >= RailCount;
        return;
    }
    const auto part=frame.slice(0,std::min<uint32_t>(m.samples,scope::InterleaveTracker::MaximumPoints));
    const auto summary=scope::Analyzer::summarize(part,scope::Channel::Ch1,true,true);
    const double rate=m.samplesPerSecond.value();
    const bool known=part.valid() && summary.valid && summary.hasFrequency && summary.frequencyHz>0 && 2*summary.frequencyHz<rate;
    interleave_.observe(known,known ? uint32_t(summary.frequencyHz+.5) : 0);
}
// Two channels at a rate above half the full rate take the compact geometry (every lane in AHB) up
// to 128K and the split one (every lane in AHB, the older words copied to AXI SRAM by the MDMA) for
// 256K and 512K; below half the full rate the standard one keeps the whole budget, and 64K two
// channels stay balanced.
scope::AcquisitionArena::Geometry DataAccess::recordGeometry() const {
    using Arena=scope::AcquisitionArena;
    if (interleaving(request_)) return Arena::interleavedGeometry(request_.recordPoints);
    const bool ch1=request_.frontend.enabled[0], ch2=request_.frontend.enabled[1];
    scope::CaptureBuffer standard;
    scope::SamplePlan plan;
    if (!Arena::record(request_.recordPoints,ch1,ch2,0,standard) || Arena::fullRate(standard))
        return Arena::geometry(request_.recordPoints,ch1,ch2,false);
    const uint32_t half=captureRateLimit()/2;
    const bool fast=scope::SamplePlan::calculate(request_.timebaseNs, request_.depth(), 2 * half, standard.ringPoints(), plan,
                                                scope::SamplePlan::FullSpeedHeadroom, 1, request_.windowPoints()) &&
                    plan.rate.value()>half;
    return Arena::geometry(request_.recordPoints,ch1,ch2,fast);
}
#ifdef OPENOSC_INTERLEAVE_PROBE
scope::InterleaveProbe::Inputs DataAccess::probeInputs(const ScopeRequest &r) const {
    return {board::Current::Features, TimerClock::apb1(), r.timebaseNs, r.frontend.enabled[0], r.frontend.enabled[1],
            r.rolling(), internalCapture(), generatorLoadsBus()};
}
// The probe's Interleave is the production rule; its SharedInput has its own conditions.
uint32_t DataAccess::interleaveProbeBlockers() const {
    return probe_.mode() == scope::ProbeMode::Interleave ? interleaveBlockers(request_) : scope::InterleaveProbe::blockers(probe_.mode(), probeInputs(request_));
}
bool DataAccess::setInterleaveProbe(scope::ProbeMode mode) {
    if (!scope::InterleaveProbe::valid(uint32_t(mode))) return false;
    if (mode == probe_.mode()) return true;
    if (scopeHeld_ || zeroBusy() || internalCapture() || recorder_.busy()) return false;
    const auto previous = probe_.mode();
    probe_.set(mode);
    if (configureScopeCore(request_, true)) return true;
    probe_.set(previous);
    configureScopeCore(request_, true);
    return false;
}
#endif
bool DataAccess::planCapture(scope::SamplePlan &plan, scope::CaptureBuffer &buffer, bool &rolling) const {
    rolling=request_.rolling() && !internalCapture();
    const bool ch1=request_.frontend.enabled[0], ch2=request_.frontend.enabled[1];
    const bool laid = internalCapture() ? scope::AcquisitionArena::trial(ch1,ch2,buffer)
        : rolling ? scope::AcquisitionArena::roll(buffer,interleaving(request_))
        : scope::AcquisitionArena::record(request_.recordPoints,ch1,ch2,0,buffer,recordGeometry(),interleaving(request_));
    return laid && samplePlan(plan,buffer,rolling);
}
bool DataAccess::samplePlan(scope::SamplePlan &plan) const {
    scope::CaptureBuffer buffer;
    bool rolling=false;
    return planCapture(plan,buffer,rolling);
}
// The acquisition the request sets up, as ACQ:MDEP?/ACQ:SRAT? and the Next card report it: points
// per channel of the plan (after the full-speed headroom or the Roll window), the timer's real
// rate and the layout these channels are captured in. A frame's own metadata says what it holds.
// A channel as the request sets the hardware, under the calibration in force (ChannelScale composes
// it): the physical range (an interleaved CH1's range correction) and, for an interleaved CH1, the
// factory's absolute frequency display gain in place of the calibration gain. The software zero is
// left out during a zero calibration's trials: they measure the factory transfer.
scope::ChannelMetadata DataAccess::scopeChannelFor(const ScopeRequest &request, unsigned c) const {
    unsigned range = c < 2 ? request.frontend.range[c] : 0;
    double gain = 0;
    if constexpr (factoryInterleave()) {
        const auto applied = appliedFrontend(request);
        if (c < 2) range = applied.physicalRange(c);
        if (c == 0 && applied.interleave && !request.rolling()) gain = interleave_.gain();
    }
    return calibration::ChannelScale::describe(scopeCalibration_,zeroFile_.values(),!zeroTrial(),request,c,range,gain);
}
void DataAccess::describeChannels(scope::FrameMetadata &metadata) {
    for (unsigned c = 0; c < 2; ++c) metadata.channels[c] = scopeChannelFor(request_, c);
}
#if OPENOSC_BOARD == 703
const scope::InterleaveMatch *DataAccess::matchFor(const scope::FrameMetadata &metadata,
                                                  const scope::FrontendSettings &frontend,
                                                  const Rational &rate) const {
    if (!frontend.interleave || frontend.rangeBoost || metadata.rolling || zeroBusy() ||
        interleaveMeasure_.active() || !interleaveCalibrated() || !metadata.channels[0].calibrated)
        return nullptr;
    return interleaveMatchProfiles_.find(TimerClock::apb1(), rate, metadata.channels[0]);
}
#endif
scope::TriggerSettings DataAccess::triggerForFrame(const scope::FrameMetadata &metadata) const {
    auto trigger = request_.trigger;
    if (!factoryInterleave() && metadata.layout == scope::Layout::InterleavedCh1 && trigger.channel == scope::Channel::Ch1) {
        const auto &channel = metadata.channels[0];
        const auto logical = scopeChannelFor(request_, 0);
        const auto convert = [&](int16_t code) {
            return int16_t(std::lround(std::clamp(scope::TriggerLevel::inFrame(code, logical, channel), -128.0, 127.0)));
        };
        trigger.level = convert(trigger.level);
        trigger.level2 = convert(trigger.level2);
    }
    return trigger;
}
bool DataAccess::describeNext(scope::FrameMetadata &metadata) {
    scope::SamplePlan plan;
    scope::CaptureBuffer buffer;
    scope::CaptureTiming timing;
    Rational rate;
    bool rolling=false;
    if (!planCapture(plan,buffer,rolling) ||
        !scope::CaptureTiming::points(TimerClock::apb1(),plan.rate,buffer.pointsPerTransfer(),timing,rate)) return false;
    auto next=metadata;
    next.samples=next.ring=plan.count; next.origin=0;
    next.samplesPerSecond=rate; next.timebaseNs=request_.timebaseNs;
    next.layout=rolling ? scope::Layout::Linear : buffer.layout(); next.rolling=rolling;
    next.interleaveOffset=next.layout==scope::Layout::InterleavedCh1 ? recordInterleaveOffset() : 0;
    next.clearTrigger();
    describeChannels(next);
#if OPENOSC_BOARD == 703
    next.interleaveMatch = interleaveMatchProfiles_.empty() ? nullptr : matchFor(next, appliedFrontend(request_), rate);
#endif
    if (!next.valid()) return false;
    metadata=next;
    return true;
}
bool DataAccess::stopCapture(bool stopClock) {
    if (!capture_.stop(stopClock)) return false;
    releaseCaptureRegion();
    return true;
}
// The memory of a capture that ended without a published record (stopped, failed, timed out) goes
// back to the arena. A region is held only while the capture mailbox is Capturing or Ready.
void DataAccess::releaseCaptureRegion() {
    if (captureRegion_ && capture_.state() == scope::FrameMailbox::State::Idle) {
        arena_.release(captureRegion_);
        captureRegion_ = 0;
    }
}
// Memory for the next capture. False with captureWaiting_ while the memory it needs still holds a
// frame someone has not seen or is reading: the next poll tries again.
bool DataAccess::reserveCapture(bool rolling, scope::CaptureBuffer &buffer) {
    using Arena=scope::AcquisitionArena;
    const bool ch1=request_.frontend.enabled[0], ch2=request_.frontend.enabled[1];
    captureWaiting_=false;
    const auto giveUpShown=[&](uint8_t blocking) {
        // The frame on show yields its memory once it has been seen, nobody reads it and no USB host
        // is still owed it (seen by the view is not read by the host).
        if (!(store_.region() & blocking) || store_.readers() || !store_.consumed() || hostOwed(Board::milliseconds()) ||
            !store_.drop()) {
            captureWaiting_=true; return false;
        }
        return true;
    };
    if (internalCapture()) {
        if (!Arena::trial(ch1,ch2,buffer)) return false;
        if (!arena_.claim(Arena::Trial) && (!giveUpShown(Arena::Trial) || !arena_.claim(Arena::Trial))) return false;
    } else if (rolling) {
        if (retainedRegion_ || !Arena::roll(buffer,interleaving(request_))) return false;
        if (store_.region() & Arena::Slots) { if (store_.readers() || !store_.drop()) { captureWaiting_=true; return false; } }
        if (!arena_.claim(Arena::RollCapture)) { captureWaiting_=true; return false; }
    } else {
        unsigned slot=0;
        const auto geometry=recordGeometry(); // The same layout the plan was made for.
        if (!arena_.freeSlot(request_.recordPoints,slot,geometry) &&
            (!giveUpShown(Arena::Slots | Arena::Roll) || !arena_.freeSlot(request_.recordPoints,slot,geometry))) {
            captureWaiting_=true; return false;
        }
        if (!Arena::record(request_.recordPoints,ch1,ch2,slot,buffer,geometry,interleaving(request_))) return false;
        if (!arena_.claim(buffer.region)) { captureWaiting_=true; return false; }
    }
    captureRegion_=buffer.region;
    return true;
}
// Active also while the capture waits for memory a frame not yet seen still holds (reserveCapture):
// the Scope view then shows that frame, which frees it, and poll starts the capture. A real
// refusal (plan, hardware) is no Scope.
// AutoSetup and zero calibration take the trial area whatever a host waits for; HOLD keeps its own
// lease. A record older than a Single arm is no reader's any more (acquireScope refuses it).
bool DataAccess::hostOwed(uint32_t now) const {
    const bool stale = beforeArm(acceptedSequence_);
    return !internalCapture() && !stale && store_.hasFrame() && store_.sequence() == acceptedSequence_ &&
           frameHosts_.owed(acceptedSequence_, acceptedAt_, now);
}
bool DataAccess::activateScope() {
    active_ = true;
    if (scopeHeld_) {
        auto frontend = appliedFrontend(request_);
        if (!factoryInterleave()) frontend.rangeBoost = false;
        if (!frontend_.scope(frontend, request_.trigger.comparatorLevel())) { active_=false; status_=DataStatus::Uncalibrated; return false; }
        if (!zeroBusy()) counters_.start();
        return true;
    }
    if (startCapture() || captureWaiting_)
        return true;
    active_ = false;
    return false;
}
bool DataAccess::startCapture() {
    captureWaiting_ = false;
    if ((scopeHeld_ && !holdPending_) || !active_ || stopping_ || captureFailed_) // A pending HOLD's retry may start.
        return false;
    // A NOR SPI transfer is admitted (StorageAdmission): its end or a later poll starts this capture,
    // as a capture waiting for memory. Held back by SPI streams whose stop was not proven, the
    // instrument reads HardwareError until the retry proves it (the start then reports Ok).
    if (StorageAdmission::active()) {
        if (StorageAdmission::stuck()) status_ = DataStatus::HardwareError;
        captureWaiting_ = true; return false;
    }
    releaseCaptureRegion(); // A faulted capture's memory first; a running capture is left alone.
    if (capture_.state() != scope::FrameMailbox::State::Idle || captureRegion_) return false;
    // The forced capture ended without a record (fault, timeout, stop for a clock change): force the next.
    if (forcedSequence_) { forcePending_ = true; forcedSequence_ = 0; }
    const bool rolling=request_.rolling() && !internalCapture();
    scope::SamplePlan plan;
    scope::CaptureBuffer buffer;
    // A start that fails is retried after CaptureRetryMs, never on the next poll: a refusal that
    // persists must not turn into a loop of starts (each one reconfigures the capture hardware).
    const auto refused = [&](DataStatus status) {
        status_ = status; captureFailed_ = true; captureRetryAt_ = Board::milliseconds() + CaptureRetryMs;
        return false;
    };
    // A host enumerates (quietCapture): a fast plan waits, as a capture waiting for memory, decided
    // from the plan alone before any memory is claimed or a shown record given up; slow records,
    // Roll and a pending HOLD's capture start.
    if (usbQuiet_ && !holdPending_) {
        scope::SamplePlan planned; scope::CaptureBuffer laid; bool roll = false;
        if (planCapture(planned, laid, roll) && fastTransfers(planned.rate.value(), laid.pointsPerTransfer())) {
            captureWaiting_ = true; return false;
        }
    }
    if (!reserveCapture(rolling, buffer)) return captureWaiting_ ? false : refused(DataStatus::Unsupported);
    if (!samplePlan(plan, buffer, rolling)) {
        arena_.release(captureRegion_); captureRegion_ = 0;
        return refused(DataStatus::Unsupported);
    }
    scope::FrameMetadata metadata;
    metadata.timestampUs = timeUs_;
    metadata.sequence = ++sequence_;
    metadata.samples = metadata.ring = plan.count;
    metadata.layout = buffer.layout();
    metadata.rolling=rolling;
    if (rolling) metadata.samples=metadata.ring=buffer.ringPoints(); // Roll runs the DMA ring continuously.
    metadata.samplesPerSecond = plan.rate;
    metadata.timebaseNs = request_.timebaseNs;
    // The budget the user's record was planned for: the status bar names it, never the request staged
    // later. Roll and internal captures (zero calibration, AutoSetup trials) name only what they hold.
    static_assert(scope::RecordBudget::Minimum % 1024 == 0 && scope::RecordBudget::Maximum / 1024 <= UINT16_MAX);
    if (!rolling && !internalCapture()) {
        metadata.budgetK = uint16_t(request_.recordPoints / 1024);
        metadata.budgetShort = plan.count < request_.depth(); // The plan keeps part of the budget spare (#138).
    }
    // The front end as this capture sets it (interleaved, CH1's range correction): its metadata says so.
    const auto frontend = appliedFrontend(request_);
    if (!frontend.interleave || rolling) interleave_.leave();
    metadata.interleaveOffset = frontend.interleave ? recordInterleaveOffset() : 0;
    describeChannels(metadata);
    const bool headroom = !factoryInterleave() && frontend.rangeBoost;
    auto trigger = request_.trigger;
    if (headroom) {
        metadata.channels[0] = calibration::ChannelScale::describe(scopeCalibration_, zeroFile_.values(), true,
                                       request_, 0, frontend.physicalRange(0));
        trigger = triggerForFrame(metadata);
    }
#if OPENOSC_BOARD == 703
    if (!interleaveMatchProfiles_.empty() && frontend.interleave && !rolling) {
        scope::CaptureTiming timing;
        Rational actualRate;
        if (scope::CaptureTiming::points(TimerClock::apb1(), plan.rate, buffer.pointsPerTransfer(), timing, actualRate))
            metadata.interleaveMatch = matchFor(metadata, frontend, actualRate);
    }
#endif
    usbtrace::mark(usbtrace::Tag::CaptureBegin);
    const bool captureStarted = capture_.start(metadata, frontend, buffer, rolling, trigger.comparatorLevel(), zeroTrial() ? nullptr : &trigger);
    usbtrace::mark(usbtrace::Tag::CaptureEnd);
    if (!captureStarted) { // Nothing was written: CaptureDma refuses before its first DMA transfer.
        arena_.release(captureRegion_); captureRegion_ = 0;
        return refused(capture_.status());
    }
    captureRestartOwed_ = false; // The fault's restart is made.
    configuration_=capture_.metadata();
    if (headroom) configuration_.channels[0] = scopeChannelFor(request_, 0);
#ifdef OPENOSC_INTERLEAVE_PROBE
    probe_.started(metadata.sequence, frontend.interleave ? scope::ProbeMode::Interleave
                                      : frontend.sharedInput ? scope::ProbeMode::SharedInput : scope::ProbeMode::Normal);
#endif
    staged_ = false; // This capture runs the current request.
    if (!zeroBusy()) counters_.start();
    configuration_.samples=configuration_.ring=plan.count;
    if (rolling) { configuration_.layout=scope::Layout::Linear; configuration_.interleaveOffset=0; }
    rollingActive_=rolling;
    if (rolling) {
        rolling_.reset(plan.count); rollingConsumed_=rollingPublished_=0; rollingLost_=false;
        rollingAt_=Board::milliseconds()-16;
    }
    if (!holdPending_) status_ = DataStatus::Ok; // A pending HOLD's retry keeps the fault until a record arrives.
    applyForce();
    return true;
}
bool DataAccess::forceScope() {
    if (!active_ || stopping_ || measurement_ != MeasurementMode::Oscilloscope || scopeHeld_ || zeroBusy() || zeroBlocked_ ||
        internalCapture())
        return false;
    if (request_.rolling()) return true;
    forcePending_ = true;
    applyForce(); // Else the next capture start takes it.
    return true;
}
void DataAccess::applyForce() {
    if (!forcePending_ || rollingActive_ || internalCapture() || capture_.state() != scope::FrameMailbox::State::Capturing ||
        !capture_.force())
        return;
    forcedSequence_ = capture_.metadata().sequence;
    // A Single capture started before the arm cannot complete it: the next capture is forced too.
    forcePending_ = request_.trigger.mode == scope::TriggerMode::Single && int32_t(forcedSequence_ - armSequence_) < 0;
}
bool DataAccess::triggerAccepts(const scope::FrameMetadata &frame, bool found) const {
    return found || frame.forced || request_.trigger.mode == scope::TriggerMode::Auto;
}
bool DataAccess::runScope() {
    if (scopeHeld_ && staged_ && !zeroBusy()) return resumeScope(request_);
    holdScope(false);
    return true;
}
void DataAccess::holdScope(bool hold) {
    revokeBackground(); // Revoked first: nothing waits for a background reader.
    if (zeroBusy()) { zeroOperation_.holdAfter(hold,Board::milliseconds()); return; }
    holdScopeCore(hold);
}
void DataAccess::holdScopeCore(bool hold) {
    if (scopeHeld_ && !hold && request_.trigger.mode == scope::TriggerMode::Single) armSequence_ = sequence_ + 1;
    if (scopeHeld_ && !hold) ++holdReleases_;
    heldTrigger_.forget();
    scopeHeld_ = hold;
    singleDone_ = false;
    forcePending_ = false; forcedSequence_ = 0; // HOLD ends a force; RUN arms without one.
    holdPending_ = false;
    if (!hold) captureFailed_ = false;
    triggerWaiting_ = !hold && request_.trigger.mode != scope::TriggerMode::Auto; acceptedTriggered_ = false;
    if (!hold) {
        heldFrame_.release();
        return;
    }
    if (!heldFrame_.valid()) store_.acquireLatest(heldFrame_);
    // With one slot (compact, 512 KiB) the frame on show was handed to the running capture: let that
    // capture finish into HOLD instead of holding nothing, for a bounded time. The same wait when the
    // last capture failed and its retry is due (captureFailed_): the frame on show is gone with it.
    const bool capturing = captureRegion_ && capture_.state() == scope::FrameMailbox::State::Capturing;
    const bool retrying = (captureFailed_ || captureRestartOwed_) && !staged_ && !request_.rolling() && capture_.state() == scope::FrameMailbox::State::Idle;
    if (!heldFrame_.valid() && !rollingActive_ && (capturing || retrying)) {
        const double record = configuration_.samplesPerSecond.valid() ? 1000.0 * configuration_.samples / configuration_.samplesPerSecond.value() : 0;
        holdPending_ = true;
        holdDeadline_ = Board::milliseconds() + uint32_t(std::min<double>(HoldWaitMs, record + 250));
        return;
    }
    stopCapture(false);
}
// Single detection, HOLD, and publication of a finished record where it lies: the store takes
// over its arena region and the capture is free to start again in other memory.
void DataAccess::serviceScopeFrames(uint32_t now) {
    serviceInterleaveOffset(now); // Its deadline and its conditions, before a record is inspected.
    // Every published frame is inspected once, also when a foreground reader (the asynchronous view,
    // its readout, USB) took it before this pass: Single must not wait for a frame nobody has read.
    // The lease lasts until the end of poll (serviceScopeCapture) and does not mark the frame seen.
    if (!zeroBusy() && !zeroBlocked_ && active_ && !stopping_ && measurement_ == MeasurementMode::Oscilloscope && !scopeHeld_ &&
        store_.acquireUninspected(inspected_)) {
        uint32_t index = 0;
        const bool fresh = !beforeArm(inspected_.view().metadata().sequence);
        const bool found=fresh && findTrigger(inspected_.view(),index);
        const bool accepted=fresh && triggerAccepts(inspected_.view().metadata(),found); // A forced record too.
        triggerWaiting_ = !accepted;
        if (accepted) { acceptedSequence_ = inspected_.view().metadata().sequence; acceptedAt_ = now; // What a reader is now given.
            acceptedTriggered_ = inspected_.view().metadata().triggered && !inspected_.view().metadata().forced; }
        observeInterleave(inspected_.view());
        // The measurement is a reader of the record it takes (the scope view may be hidden by Settings):
        // it consumes it, so a one-slot capture memory is free for the next record.
        if (interleaveMeasure_.record(inspected_.view())) {
            store_.consume(inspected_.view().metadata().sequence);
            if (!interleaveMeasure_.active()) finishInterleaveOffset();
        }
        if (accepted && request_.trigger.mode == scope::TriggerMode::Single) {
            inspected_.annotateTrigger(found, index, triggerForFrame(inspected_.view().metadata()).tag());
            heldTrigger_.remember(inspected_.view().metadata().sequence, found, index, inspected_.view().metadata().triggerEdge);
            scopeHeld_ = true; singleDone_ = true; triggerWaiting_ = false; forcePending_ = false;
            stopCapture(false); heldFrame_ = std::move(inspected_);
        }
    }
    if (!zeroBusy() && !zeroBlocked_ && scopeHeld_ && !heldFrame_.valid())
        store_.acquireLatest(heldFrame_);
    // A pending HOLD holds the next record its captures deliver before holdDeadline_. A capture that
    // fails (a recoverable fault: HardwareError, Timeout, Overflow) is retried after CaptureRetryMs
    // within that time (serviceScopeCapture), never past it; a staged change waits for RUN instead,
    // so its fault ends the wait at once. Out of time: HOLD with nothing and the fault, as before.
    const bool ended = capture_.state() == scope::FrameMailbox::State::Idle; // Faulted or timed out.
    if (holdPending_ && (heldFrame_.valid() || int32_t(now - holdDeadline_) >= 0 || (ended && staged_))) {
        holdPending_ = false;
        if (heldFrame_.valid()) status_ = DataStatus::Ok; // A retry delivered: the fault is behind it.
        else if (ended && capture_.status() != DataStatus::Ok) status_ = capture_.status();
        stopCapture(false);
    }
    if (capture_.state() == scope::FrameMailbox::State::Ready && (!scopeHeld_ || holdPending_) && store_.writable()) {
        scope::FrameLease frame;
        if (capture_.acquire(frame) && store_.adopt(frame.view(), captureRegion_)) {
            captureRegion_ = 0;
            if (frame.view().metadata().sequence == forcedSequence_) forcedSequence_ = 0; // Its record is in.
            scope::TriggerSettings searched; // Its publication already searched it: not a second pass.
            if (capture_.edgeless(frame.view().metadata(), searched)) triggerCache_.remember(frame.view(), searched, false, 0);
        }
        frame.release();
        releaseCaptureRegion();
        if (holdPending_) store_.acquireLatest(heldFrame_);
    }
    followTriggerWait(now);
}
// WAIT again after an accepted record: Normal (or a Single not yet complete) whose running capture
// has been Armed - its pre-trigger part filled, an edge all it waits for - for TriggerWaitMs. On a
// signal that triggers each capture the gate is Armed for at most the time to the next edge, so
// the state stays RUN (trigger rates below about 1000 / TriggerWaitMs Hz do read WAIT between
// edges, as the scope then is waiting). Filling, Post, a restart, a capture waiting for memory or
// paused prove nothing: the state stays as it is, so restarts and clock changes never toggle it.
// An accepted record ends WAIT (serviceScopeFrames); configure, RUN and HOLD set it as before.
// Times are modular milliseconds; a capture is followed by its sequence.
void DataAccess::followTriggerWait(uint32_t now) {
    const bool armed = request_.trigger.mode != scope::TriggerMode::Auto && active_ && !stopping_ && !scopeHeld_ &&
                       measurement_ == MeasurementMode::Oscilloscope && !internalCapture() && capture_.awaitingTrigger();
    const uint32_t misses = capture_.misses();
    if (misses != missesSeen_) { // Candidates rearmed since the last pass: waiting, once that lasts.
        missesSeen_ = misses;
        if (request_.trigger.mode != scope::TriggerMode::Auto && !scopeHeld_ && uint32_t(now - acceptedAt_) >= TriggerWaitMs)
            triggerWaiting_ = true;
    }
    if (!armed) { armedSeen_ = false; return; }
    const uint32_t sequence = capture_.metadata().sequence;
    if (!armedSeen_ || armedSequence_ != sequence) { armedSeen_ = true; armedSequence_ = sequence; armedAt_ = now; return; }
    if (uint32_t(now - armedAt_) >= TriggerWaitMs) triggerWaiting_ = true;
}
// Fault handling and restart. A faulted capture has cancelled its mailbox; its memory is returned
// before the retry, so neither the 512 KiB arena nor a slot can stay claimed by a dead capture.
void DataAccess::serviceScopeCapture(uint32_t now) {
    inspected_.release();
    releaseCaptureRegion();
    if (!active_ || stopping_ || measurement_ != MeasurementMode::Oscilloscope || (scopeHeld_ && !holdPending_)) return;
    // Taken again every pass while it stands (a stop not yet proven keeps re-arming the back-off).
    if (capture_.status() == DataStatus::HardwareError || capture_.status() == DataStatus::Timeout || capture_.status()==DataStatus::Overflow) {
        captureRestartOwed_ = true;
        status_ = capture_.status();
        if (!captureFailed_) captureRetryAt_ = now + CaptureRetryMs;
        captureFailed_ = true;
    }
    // A transient bus or interrupt-latency fault must not stop the instrument for good.
    if (captureFailed_ && int32_t(now-captureRetryAt_)>=0) captureFailed_ = false;
    const auto zeroPhase=zeroOperation_.phase();
    // Not before a zero's trial is configured, nor while its original returns; a configured trial
    // whose start was deferred must start, or it never reaches the hardware.
    const bool mayCapture=(zeroPhase!=calibration::ZeroOperation::Phase::Applying || zeroOperation_.trialConfigured()) &&
                          zeroPhase!=calibration::ZeroOperation::Phase::Restoring;
    if (mayCapture && capture_.state() == scope::FrameMailbox::State::Idle && !captureFailed_ &&
        uint32_t(now - configuredAt_) >= ConfigQuietMs && !capturePaused_)
        startCapture();
}
} // namespace openosc
