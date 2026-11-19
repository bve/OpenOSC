// Diagnostic candidate17 only: compiled empty unless OPENOSC_DIAG_MAILBOX (env firmware_diag).
#ifdef OPENOSC_DIAG_MAILBOX
#include "openosc/application/local_control.hpp"
#include "openosc/application/diag_record.hpp"
#include "openosc/acquisition/ranges.hpp"
#include <atomic>
#include <cmath>

extern "C" {
__attribute__((used)) volatile openosc::diag::Mailbox openosc_diag_mailbox{};
}

namespace openosc::diag {
namespace {
volatile Mailbox &box() { return openosc_diag_mailbox; }
bool faulted(DataStatus status) {
    return status == DataStatus::HardwareError || status == DataStatus::Timeout || status == DataStatus::Overflow;
}
} // namespace

LocalControl::LocalControl(scpi::Backend &backend) : backend_(backend) {
    auto &m = box();
    m.version = Version;
    m.size = sizeof(Mailbox);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    m.magic = Magic;
}

void LocalControl::poll(uint32_t now) {
    ++heartbeat_;
    const Phase before = phase_;
    watch(); // HOLD first: before a command is served, a lease given back or a sample read.
    const uint32_t command = box().command;
    const bool served = uint8_t(command >> 8) && uint8_t(command >> 8) != ack_;
    PerformanceSnapshot performance{};
    if (served) {
        backend_.performance(performance);
        serve(command, now);
    }
    if (phase_ == Phase::Describing) describe();
    const bool due = uint32_t(now - reportAt_) >= ReportMs || served || phase_ != before ||
                     phase_ == Phase::Holding || (phase_ == Phase::Running && uint32_t(now - runAt_) >= Stages[stage_].durationMs);
    if (!due) return;
    backend_.performance(performance);
    const auto progress = backend_.captureProgress();
    if (phase_ == Phase::Running) running(now, performance, progress);
    if (phase_ == Phase::Holding) holding(now, backend_.captureProgress()); // Fresh after a stop in this pass.
    watch();
    report(now, performance, progress);
}

void LocalControl::serve(uint32_t command, uint32_t now) {
    ack_ = uint8_t(command >> 8);
    if (phase_ == Phase::Failed) { // Acknowledged, refused; the failure reason stays the result.
        if (Code(command & 0xff) == Code::Stop && !backend_.held()) backend_.stop();
        return;
    }
    result_ = Reason::None;
    switch (Code(command & 0xff)) {
    case Code::Stop:
        if (phase_ == Phase::Idle) { stage_ = 0; stopStage(now); return; }
        if (phase_ == Phase::Running) { aborted_ = true; stopStage(now); return; }
        result_ = Reason::WrongPhase;
        return;
    case Code::AckStopped:
        if (phase_ != Phase::Held) { result_ = Reason::WrongPhase; return; }
        index_ = 0; crc_[0] = Crc32(); crc_[1] = Crc32();
        phase_ = Phase::Describing;
        return;
    case Code::AckDone:
        if (phase_ != Phase::Described) { result_ = Reason::WrongPhase; return; }
        backend_.releaseFrame(); view_ = nullptr;
        completed_ = stage_;
        phase_ = stage_ == 2 ? Phase::Done : Phase::Released;
        return;
    case Code::Start64:
    case Code::Start512: {
        const unsigned stage = Code(command & 0xff) == Code::Start64 ? 1 : 2;
        if (phase_ != Phase::Released) { result_ = Reason::WrongPhase; return; }
        if (aborted_ || completed_ != stage - 1) { result_ = Reason::Order; return; }
        PerformanceSnapshot performance{};
        backend_.performance(performance);
        result_ = start(stage, now, performance);
        if (result_ != Reason::None) fail(result_);
        return;
    }
    default:
        result_ = Reason::UnknownCommand;
        return;
    }
}

Reason LocalControl::channels() const {
    const auto &r = backend_.scopeRequest();
    for (unsigned c = 0; c < 2; ++c)
        if (!r.frontend.enabled[c] || scope::VoltageRanges::millivolts(r.frontend.range[c]) != ChannelMillivolts ||
            r.frontend.coupling[c] != scope::Coupling::Dc || r.probeNumerator[c] != 1 || r.probeDenominator[c] != 1)
            return Reason::Channels;
    return Reason::None;
}

// The SCPI GEN sequence's end state (OUTP OFF, SOUR:FUNC SQU, SOUR:LEV:PROF DED, SOUR:FREQ 100000,
// SOUR:PULS:DCYC 50, OUTP ON) through the same backend calls, in one configuration.
Reason LocalControl::generator() {
    if (!backend_.output(false)) return Reason::Generator;
    auto settings = backend_.generator().requested;
    settings.shape = generator::Shape::Square;
    settings.level = generator::Level::Dedicated;
    settings.frequencyHz = {100000000, 1000};
    settings.dutyPermille = 500;
    if (!backend_.configureGenerator(settings) || !backend_.output(true)) return Reason::Generator;
    return generatorReady() ? Reason::None : Reason::Generator;
}

bool LocalControl::generatorReady() const {
    const auto g = backend_.generator();
    const auto &s = g.requested;
    return g.enabled && s.shape == generator::Shape::Square && s.level == generator::Level::Dedicated && s.dutyPermille == 500 &&
           s.frequencyHz.denominator && uint64_t(s.frequencyHz.numerator) == 100000ULL * s.frequencyHz.denominator;
}

Reason LocalControl::start(unsigned stage, uint32_t now, const PerformanceSnapshot &performance) {
    if (!backend_.held()) return Reason::NotHeld;
    if (performance.coreHz != CoreHz) return Reason::Clock;
    if (const Reason r = channels(); r != Reason::None) return r;
    const auto *metadata = backend_.configuration();
    if (!metadata || !metadata->channels[0].calibrated) return Reason::Calibration;
    if (stage == 1) { if (const Reason r = generator(); r != Reason::None) return r; }
    else if (!generatorReady()) return Reason::Generator;
    // The SCPI setup (TRIG:EDGE:SOUR CHAN1, SLOP POS, LEV 1.5, SWE AUTO, TIM:SCAL 5e-6, ACQ:MDEP) as one
    // request, staged while held: the configuration of the next RUN, never applied over a capture.
    ScopeRequest request = backend_.scopeRequest();
    request.trigger.channel = scope::Channel::Ch1;
    request.trigger.edge = scope::Edge::Rising;
    request.trigger.mode = scope::TriggerMode::Auto;
    const auto &ch1 = metadata->channels[0];
    const double code = TriggerVolts / (ch1.voltsPerCode * ch1.probeNumerator / ch1.probeDenominator) + ch1.zeroCode;
    if (!(code >= -128 && code <= 127)) return Reason::Calibration;
    request.trigger.level = int16_t(std::lround(code));
    request.timebaseNs = TimebaseNs;
    request.recordPoints = scope::RecordBudget::covering(Stages[stage].requestPoints, request.channels());
    if (!request.recordPoints || !backend_.setTrigger(request) || !backend_.held()) return Reason::Configure; // Its level is set.
    const auto *plan = backend_.configuration();
    if (!plan || plan->samples != Stages[stage].planSamples || !plan->samplesPerSecond.valid() ||
        uint64_t(plan->samplesPerSecond.numerator) != uint64_t(Rate) * plan->samplesPerSecond.denominator)
        return Reason::Plan;
    const auto progress = backend_.captureProgress();
    const auto &r = backend_.scopeRequest();
    const auto g = backend_.generator();
    Config config{};
    config.stage = stage;
    config.coreHz = performance.coreHz;
    config.budget = r.recordPoints;
    config.planSamples = plan->samples;
    config.rateNumerator = plan->samplesPerSecond.numerator;
    config.rateDenominator = plan->samplesPerSecond.denominator;
    config.timebaseNs = uint32_t(r.timebaseNs);
    config.channels = uint32_t(r.frontend.enabled[0]) | uint32_t(r.frontend.enabled[1]) << 1 |
                      uint32_t(r.frontend.coupling[0]) << 2 | uint32_t(r.frontend.coupling[1]) << 3;
    for (unsigned c = 0; c < 2; ++c) config.millivolts[c] = scope::VoltageRanges::millivolts(r.frontend.range[c]);
    config.probes = (r.probeNumerator[0] & 0xff) | (r.probeDenominator[0] & 0xff) << 8 | (r.probeNumerator[1] & 0xff) << 16 |
                    (r.probeDenominator[1] & 0xff) << 24;
    config.trigger = uint32_t(r.trigger.channel) | uint32_t(r.trigger.edge) << 4 | uint32_t(r.trigger.mode) << 8 |
                     uint32_t(uint16_t(r.trigger.level)) << 16;
    config.generator = uint32_t(g.enabled) | uint32_t(g.requested.shape) << 4 | uint32_t(g.requested.level) << 8 |
                       uint32_t(g.requested.dutyPermille) << 16;
    config.generatorNumerator = g.requested.frequencyHz.numerator;
    config.generatorDenominator = g.requested.frequencyHz.denominator;
    config.startSequence = progress.sequence;
    config.startCaptures = performance.captures;
    config.durationMs = Stages[stage].durationMs;
    publish(box().configPublish, box().config, config, configPublish_);
    stage_ = stage;
    startSequence_ = progress.sequence;
    captures_ = performance.captures; captureAt_ = now;
    if (!backend_.run(false)) return Reason::Run;
    runAt_ = now; stageMs_ = 0;
    phase_ = Phase::Running;
    return Reason::None;
}

void LocalControl::running(uint32_t now, const PerformanceSnapshot &performance, const scope::CaptureProgress &progress) {
    if (performance.captures != captures_) { captures_ = performance.captures; captureAt_ = now; }
    stageMs_ = now - runAt_;
    if (backend_.held()) { fail(Reason::UnexpectedHold); return; }
    if (faulted(progress.status)) { stopStage(now); fail(Reason::CaptureStatus); return; }
    if (stageMs_ >= Stages[stage_].durationMs) stopStage(now);
}

// The ordinary HOLD, as SCPI STOP: the record of the running capture is kept, the capture stops.
void LocalControl::stopStage(uint32_t now) {
    if (phase_ == Phase::Running) stageMs_ = now - runAt_;
    capturesAtStop_ = captures_;
    msSinceCapture_ = now - captureAt_;
    stopAt_ = now;
    backend_.stop();
    phase_ = Phase::Holding;
    holdAt_ = now;
}

// Logical HOLD with the capture no longer active, and (after a stage) a lease on the held record. A
// lease reads no sample; the record is read only after the host proved the physical stop (AckStopped).
void LocalControl::holding(uint32_t now, const scope::CaptureProgress &progress) {
    if (backend_.held() && !progress.active) {
        if (!view_) view_ = backend_.frame(false);
        if (view_ || stage_ == 0) { phase_ = Phase::Held; return; }
    }
    if (uint32_t(now - holdAt_) > HoldTimeoutMs) fail(backend_.held() && !progress.active ? Reason::NoFrame : Reason::HoldTimeout);
}

// CRC32 of what WAV:DATA would send, uint8(signedCode + 128) for every sample of each enabled channel,
// through the published FrameView (its AdcBusGlitch rule included), a chunk per main-loop pass.
void LocalControl::describe() {
    if (!view_) {
        if (stage_) { fail(Reason::NoFrame); return; }
        described();
        return;
    }
    const auto &m = view_->metadata();
    if (stage_ && int32_t(m.sequence - startSequence_) <= 0) { fail(Reason::Stale); return; }
    const uint32_t end = m.samples - index_ > CrcChunk ? index_ + CrcChunk : m.samples;
    uint8_t buffer[256];
    for (unsigned c = 0; c < 2; ++c) {
        if (!m.channels[c].enabled) continue;
        for (uint32_t i = index_; i < end;) {
            const uint32_t n = end - i > sizeof buffer ? uint32_t(sizeof buffer) : end - i;
            for (uint32_t k = 0; k < n; ++k) {
                int16_t code;
                if (!view_->signedCode(scope::Channel(c), i + k, code)) { fail(Reason::NoFrame); return; }
                buffer[k] = uint8_t(code + 128);
            }
            crc_[c].update(buffer, n);
            i += n;
        }
    }
    index_ = end;
    if (index_ >= m.samples) described();
}

void LocalControl::described() {
    Descriptor d{};
    if (view_) {
        const auto &m = view_->metadata();
        frameFields(*view_, d);
        d.timestampLow = uint32_t(m.timestampUs); d.timestampHigh = uint32_t(m.timestampUs >> 32);
        d.timebaseLow = uint32_t(m.timebaseNs); d.timebaseHigh = uint32_t(m.timebaseNs >> 32);
        d.crc[0] = crc_[0].value(); d.crc[1] = crc_[1].value();
    }
    d.capturesAtStop = capturesAtStop_; d.msSinceCapture = msSinceCapture_; d.stopUptimeMs = stopAt_; d.stageMs = stageMs_;
    publish(box().descriptorPublish, box().descriptor, d, descriptorPublish_);
    phase_ = Phase::Described;
}

// Between the stop and the next start the scope must stay held: nothing else may restart the capture
// over the record the host is checking.
void LocalControl::watch() {
    switch (phase_) {
    case Phase::Held: case Phase::Describing: case Phase::Described: case Phase::Released: case Phase::Done:
        if (!backend_.held()) fail(Reason::UnexpectedRun);
        break;
    default: break;
    }
}

void LocalControl::fail(Reason reason) {
    if (!backend_.held()) backend_.stop(); // The ordinary HOLD; a retained lease stays owned.
    result_ = reason;
    phase_ = Phase::Failed; // HOLD, the lease and the record stay; no start is served again.
}

void LocalControl::report(uint32_t now, const PerformanceSnapshot &performance, const scope::CaptureProgress &progress) {
    reportAt_ = now;
    auto &t = box().telemetry;
    const uint32_t count = ++telemetryCount_;
    t.begin = count; // Highest word first: a host block read that overlaps this sees end != begin.
    std::atomic_thread_fence(std::memory_order_seq_cst);
    t.state = packState(phase_, stage_, view_ != nullptr, ack_, result_, progress.status, progress.active);
    t.heartbeat = heartbeat_;
    t.uptimeMs = now;
    t.stageMs = phase_ == Phase::Running ? now - runAt_ : stageMs_;
    t.captures = performance.captures;
    t.projections = performance.projections;
    t.flushes = performance.flushes;
    t.sequence = progress.sequence;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    t.end = count; // Lowest word last: the record is complete.
}
} // namespace openosc::diag
#endif
