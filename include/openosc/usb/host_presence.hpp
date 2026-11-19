#pragma once
#include <cstdint>

namespace openosc::usb {
// Whether a USB host is on the bus, proven by its start-of-frame stream: a full-speed host sends one
// SOF per 1 ms frame, and the device core keeps the last frame number it received (DSTS.FNSOF;
// at full speed an 11-bit number, so it wraps at 2048). Bus-reset counts, SETUP counts, a configured
// state left from before and self-reconnects are not evidence of a host: a device whose cable was
// pulled stays attached with frozen frames (HIL 2026-11-19,
// build/reviews/issue23-200mhz-display/hardware/cable-observe.json).
// - sample(): the frame number read at `now` (the tick of that read). Spans shorter than MinSpanMs
//   wait for more; a span is valid when the frames advanced by its length within Tolerance (tick and
//   SOF clocks, the read itself); spans longer than MaxSpanMs (a stalled loop) give no verdict.
// - present(): two valid spans, the last one less than GraceMs ago. The grace covers a host's bus
//   reset, which sends no SOF (root port reset 50 ms, USB 2.0 7.1.7.5), and ordinary loop gaps; a suspended host (no SOF) and an
//   unplugged one end presence after it, so neither gets a forced reconnect.
// Modular milliseconds.
class HostPresence {
public:
    static constexpr uint32_t FrameMask = 0x7ff;
    static constexpr uint32_t MinSpanMs = 4, MaxSpanMs = 1000, Tolerance = 2, GraceMs = 100;
    void sample(uint32_t now, uint32_t frame) {
        frame &= FrameMask;
        if (!sampled_) { sampled_ = true; at_ = now; frame_ = frame; return; }
        const uint32_t span = now - at_;
        if (span < MinSpanMs) return;
        if (span <= MaxSpanMs) {
            const uint32_t advanced = (frame - frame_) & FrameMask;
            if (advanced + Tolerance >= span && advanced <= span + Tolerance) {
                if (proofs_ < 2) ++proofs_;
                if (proofs_ >= 2) seen_ = true;
                provenAt_ = now;
            } else if (uint32_t(now - provenAt_) >= GraceMs) proofs_ = 0;
        } else proofs_ = 0;
        at_ = now; frame_ = frame;
    }
    bool present(uint32_t now) const { return proofs_ >= 2 && uint32_t(now - provenAt_) < GraceMs; }
    // A host proven since construction or the last mark() (an attach): one that may still be enumerating.
    void mark() { seen_ = false; }
    bool seen() const { return seen_; }
    // How long no host has been proven: 0 while present.
    uint32_t absentFor(uint32_t now) const { return present(now) ? 0 : uint32_t(now - provenAt_); }

private:
    uint32_t at_ = 0, frame_ = 0, provenAt_ = 0;
    uint8_t proofs_ = 0;
    bool sampled_ = false, seen_ = false;
};
} // namespace openosc::usb
