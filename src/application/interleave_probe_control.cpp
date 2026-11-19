// Diagnostic build only: compiled empty unless OPENOSC_INTERLEAVE_PROBE (env firmware_interleave_probe).
#ifdef OPENOSC_INTERLEAVE_PROBE
#include "openosc/application/interleave_probe_control.hpp"
#include "openosc/application/diag_record.hpp"
#include <atomic>
#include <cstring>

extern "C" {
__attribute__((used)) volatile openosc::diag::ProbeMailbox openosc_interleave_probe{};
}

namespace openosc::diag {
InterleaveProbeControl::InterleaveProbeControl(ProbeTarget &target, scpi::Backend &backend, volatile ProbeMailbox &box)
    : target_(target), backend_(backend), box_(box) {
    box_.version = ProbeVersion;
    box_.size = sizeof(ProbeMailbox);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    box_.magic = ProbeMagic;
}

void InterleaveProbeControl::poll() {
    ++heartbeat_;
    // A RUN (SCPI, keys) ends the hold: the record is given back and its description withdrawn.
    if (view_ && !backend_.held()) release();
    const uint32_t command = box_.command;
    const uint8_t sequence = uint8_t(command >> 8);
    if (sequence && sequence != ack_) {
        ack_ = sequence;
        result_ = serve(ProbeCode(command & 0xff));
    }
    const auto &probe = target_.interleaveProbe();
    const uint32_t blockers = target_.interleaveProbeBlockers();
    box_.blockers = blockers;
    box_.heartbeat = heartbeat_;
    box_.state = packProbeState(probe.mode(), blockers ? scope::ProbeMode::Normal : probe.mode(), view_ != nullptr, ack_, result_);
}

ProbeResult InterleaveProbeControl::serve(ProbeCode code) {
    switch (code) {
    case ProbeCode::Normal: case ProbeCode::SharedInput: case ProbeCode::Interleave:
        // Refused while held (and the lease stays): RUN first, then the new mode, then HOLD again.
        return target_.setInterleaveProbe(scope::ProbeMode(uint8_t(code) - 1)) ? ProbeResult::Ok : ProbeResult::Refused;
    case ProbeCode::Describe: return describe();
    case ProbeCode::Release: release(); return ProbeResult::Ok;
    default: return ProbeResult::UnknownCommand;
    }
}

// The held record through the ordinary backend's frame lease (as SCPI's WAV:DATA reads it), kept until
// Release or a RUN; its raw words stay where the descriptor says while leased.
ProbeResult InterleaveProbeControl::describe() {
    if (!backend_.held()) return ProbeResult::NotHeld;
    if (view_) release();
    view_ = backend_.frame(false);
    if (!view_ || !view_->valid()) {
        if (view_) backend_.releaseFrame();
        view_ = nullptr;
        return ProbeResult::NoFrame;
    }
    ProbeFrame d{};
    frameFields(*view_, d);
    d.recordMode = target_.interleaveProbe().recordMode(view_->metadata().sequence);
    const float offset = view_->metadata().interleaveOffset;
    std::memcpy(&d.interleaveOffsetBits, &offset, sizeof offset);
    publish(box_.framePublish, box_.frame, d, publish_);
    return ProbeResult::Ok;
}

void InterleaveProbeControl::release() {
    if (view_) backend_.releaseFrame();
    view_ = nullptr;
    publish(box_.framePublish, box_.frame, ProbeFrame{}, publish_); // frameValid 0.
}
} // namespace openosc::diag
#endif
