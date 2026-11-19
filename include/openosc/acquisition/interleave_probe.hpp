#pragma once
// Diagnostic only: the ZT703S interleave feasibility probe (env firmware_interleave_probe,
// OPENOSC_INTERLEAVE_PROBE; docs/INTERLEAVE_703_PROBE.md). Release images do not use it; the ZT706S
// interleave (InterleavePolicy) and the ordinary ZT703S capture are untouched while it is Normal.
//   Normal:      the build's ordinary capture (on the ZT703S: two independent ADC paths).
//   SharedInput: the factory selector (control bits 8 and 11, 0804A96C as its calibration 08032914
//                calls it): ADC B on CH1 through the SGM3157, data alignment; the ADC clocks stay in
//                phase and the two-channel layout stays, so each ADC is seen apart (CH2's bytes are ADC B).
//   Interleave:  the production ZT703S interleave (InterleaveRule, SelectableInterleave) selected by the
//                probe in place of the user's setting: InterleavedWords with opposed ADC clocks, CH1 alone,
//                twice the profile's ADC rate at its ordinary DMA word rate (200/100: 200 MS/s at 100 M
//                words/s); no ZT706S range correction or gain curve; the ZT703S's own offset.
#include "openosc/acquisition/interleave.hpp"
#include "openosc/board/capabilities.hpp"
#include "openosc/settings/run_mode.hpp"
#include <cstdint>

namespace openosc::scope {
enum class ProbeMode : uint8_t { Normal, SharedInput, Interleave, Count };

class InterleaveProbe {
public:
    static constexpr uint32_t TimerHz = RunModes[0].coreHz; // SharedInput: 200 MHz, each ADC at 100 MHz.
    static_assert(RunModes[0].coreHz == 200000000 && RunModes[0].adcDivider == 2, "SharedInput runs on the 200/100 profile");
    // What keeps a mode from applying to a request (0: it applies; Normal always applies).
    // The shared bits (InterleaveConditions); SharedInput uses Board, Clock, Channels and Internal.
    enum Blocker : uint32_t {
        Board = InterleaveConditions::Board, Clock = InterleaveConditions::Clock, Timebase = InterleaveConditions::Timebase,
        Channels = InterleaveConditions::Channels, Rolling = InterleaveConditions::Rolling,
        Internal = InterleaveConditions::Internal, BusLoaded = InterleaveConditions::BusLoaded,
    };
    struct Inputs {
        board::Capabilities board;
        uint32_t timerHz;
        uint64_t timebaseNs;
        bool ch1, ch2, rolling, internal, busLoaded;
    };
    // The ZT703S only: the board with the selectable interleave.
    static constexpr bool board(const board::Capabilities &c) { return c.selectableInterleave; }
    static constexpr uint32_t blockers(ProbeMode mode, const Inputs &in) {
        if (mode == ProbeMode::Normal) return 0;
        if (mode == ProbeMode::Interleave)
            return board(in.board) ? InterleaveRule::blockers(in.board, true, {in.timerHz, in.timebaseNs, in.ch1, in.ch2, in.rolling,
                                                                                in.internal, in.busLoaded})
                                   : uint32_t(Board);
        return (board(in.board) ? 0U : uint32_t(Board)) | (in.timerHz == TimerHz ? 0U : uint32_t(Clock)) |
               (in.internal ? uint32_t(Internal) : 0U) | (in.ch1 && in.ch2 ? 0U : uint32_t(Channels));
    }
    static constexpr bool valid(uint32_t mode) { return mode < uint32_t(ProbeMode::Count); }

    ProbeMode mode() const { return mode_; }
    bool active() const { return mode_ != ProbeMode::Normal; }
    void set(ProbeMode mode) { mode_ = mode; }
    // The mode a request gets now: the requested one where nothing blocks it, else Normal.
    ProbeMode applied(const Inputs &in) const { return blockers(mode_, in) ? ProbeMode::Normal : mode_; }
    // Each capture start: its sequence (its records carry it) and the mode it was started in. Starts in
    // the same mode form one run; the current and the previous run are kept, so a record held from
    // either says its mode (Unknown: older, or before any start). Modular sequences.
    void started(uint32_t sequence, ProbeMode applied) {
        if (runs_ && applied == current_.mode) return;
        previous_ = current_; current_ = {sequence, applied};
        if (runs_ < 2) ++runs_;
    }
    static constexpr uint8_t Unknown = 0xff;
    uint8_t recordMode(uint32_t sequence) const {
        if (runs_ >= 1 && int32_t(sequence - current_.from) >= 0) return uint8_t(current_.mode);
        if (runs_ >= 2 && int32_t(sequence - previous_.from) >= 0) return uint8_t(previous_.mode);
        return Unknown;
    }

private:
    struct Run { uint32_t from = 0; ProbeMode mode = ProbeMode::Normal; };
    ProbeMode mode_ = ProbeMode::Normal;
    Run current_, previous_;
    uint8_t runs_ = 0;
};
} // namespace openosc::scope
