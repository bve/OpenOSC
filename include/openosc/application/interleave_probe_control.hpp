#pragma once
// Diagnostic build only (env firmware_interleave_probe, OPENOSC_INTERLEAVE_PROBE): the ZT703S interleave
// probe's SWD mailbox (docs/INTERLEAVE_703_PROBE.md). The host writes one command word; the main loop
// sets the probe mode through DataAccess (an ordinary forced reconfigure) and describes a HELD record
// through the instrument's ordinary backend (frame lease), so the host can read its raw words over SWD.
// Nothing else: scope settings, RUN and HOLD come from the ordinary paths (SCPI over USB, the keys).
#include "openosc/acquisition/interleave_probe.hpp"
#include "openosc/scpi/backend.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::diag {
// ABI version 1 of `openosc_interleave_probe`: 39 little-endian 32-bit words (.bss, zero at boot: mode
// Normal). The host checks the symbol's address and size in this image's ELF, then magic, version and
// size, before its first write.
// - command: host only, sequence (1..255) << 8 | code; a new sequence is served once.
// - state: rewritten every main-loop pass: requested mode bits 0-1, applied mode (for the request in
//   force) bits 2-3, record leased bit 4, acknowledged sequence bits 8-15, its result bits 16-23.
// - blockers: InterleaveProbe::Blocker bits keeping the requested mode from the request in force.
// - frame: behind framePublish (odd while written, then even and larger), closed by a CRC32 of its
//   other words. Valid only while the record stays leased (Describe .. Release, or a RUN ends it).
enum class ProbeCode : uint8_t { None, Normal, SharedInput, Interleave, Describe, Release };
enum class ProbeResult : uint8_t { None, Ok, Refused, NotHeld, NoFrame, UnknownCommand };
struct ProbeFrame {
    uint32_t frameValid, sequence, samples, ring, origin, layout, rateNumerator, rateDenominator, flags, triggerIndex,
             banks[4], ahbWords, historyWords, older[4], end[4], newest[4],
             recordMode,          // ProbeMode the record was captured in, 0xff unknown (InterleaveProbe::recordMode).
             interleaveOffsetBits, // FrameMetadata::interleaveOffset (float bits): 0 in every probe record.
             check;
};
struct ProbeMailbox {
    uint32_t magic, version, size, command, state, blockers, heartbeat, framePublish;
    ProbeFrame frame;
};
constexpr uint32_t ProbeMagic = 0x50373049U, ProbeVersion = 1; // "I07P"
static_assert(sizeof(ProbeFrame) == 31 * 4 && sizeof(ProbeMailbox) == 39 * 4, "probe mailbox ABI v1 size");
static_assert(offsetof(ProbeMailbox, command) == 12 && offsetof(ProbeMailbox, state) == 16 &&
              offsetof(ProbeMailbox, blockers) == 20 && offsetof(ProbeMailbox, framePublish) == 28 &&
              offsetof(ProbeMailbox, frame) == 32 && offsetof(ProbeFrame, banks) == 10 * 4 &&
              offsetof(ProbeFrame, older) == 16 * 4 && offsetof(ProbeFrame, recordMode) == 28 * 4 &&
              offsetof(ProbeFrame, check) == 30 * 4, "probe mailbox ABI v1 offsets");
constexpr uint32_t packProbeState(scope::ProbeMode requested, scope::ProbeMode applied, bool leased, uint8_t ack,
                                  ProbeResult result) {
    return uint32_t(requested) | uint32_t(applied) << 2 | uint32_t(leased) << 4 | uint32_t(ack) << 8 |
           uint32_t(result) << 16;
}

// The instrument side the probe sets (DataAccess in the firmware).
class ProbeTarget {
public:
    virtual bool setInterleaveProbe(scope::ProbeMode mode) = 0;
    virtual const scope::InterleaveProbe &interleaveProbe() const = 0;
    virtual uint32_t interleaveProbeBlockers() const = 0;
protected:
    ~ProbeTarget() = default;
};

class InterleaveProbeControl {
public:
    InterleaveProbeControl(ProbeTarget &target, scpi::Backend &backend, volatile ProbeMailbox &box);
    void poll();

private:
    ProbeResult serve(ProbeCode code);
    ProbeResult describe();
    void release();
    ProbeTarget &target_;
    scpi::Backend &backend_;
    volatile ProbeMailbox &box_;
    const scope::FrameView *view_ = nullptr;
    uint32_t heartbeat_ = 0, publish_ = 0;
    uint8_t ack_ = 0;
    ProbeResult result_ = ProbeResult::None;
};
} // namespace openosc::diag

extern "C" volatile openosc::diag::ProbeMailbox openosc_interleave_probe;
