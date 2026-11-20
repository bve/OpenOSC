#pragma once
#include "openosc/board/capabilities.hpp"
#include "openosc/settings/run_mode.hpp"
#include <cstdint>

namespace openosc::scope {
enum class InterleaveMode : uint8_t { Off, On, Auto };
// Interleave: CH1 alone through both ADCs clocked in opposite phase (control bits 8/11, TIM1 CC3P,
// InterleavedWords, Layout::InterleavedCh1 at two points a word, twice the ADC rate). The capture follows
// from InterleaveRule in DataAccess, AnalogFrontend, AdcClock and CaptureDma. Two boards, two rules:
// - ZT706S, the factory's (zt_fw 706s_main): InterleavePolicy at 280 MHz (updater 0804A9A8), with its
//   display gain InterleaveResponse (0804D6A4/0804D720) and range correction InterleaveRangeBoost (0804F3F0);
// - ZT703S, the user's choice (Capabilities::selectableInterleave; the factory 703S never enables it):
//   SelectableInterleave on every run mode, none of the ZT706S curves, its own offset calibration.
// What every interleave needs of a request (both boards), as blockers: none, it may run.
struct InterleaveConditions {
    enum Blocker : uint32_t {
        Board = 1, Clock = 2, Timebase = 4, Channels = 8, Rolling = 16, Internal = 32, BusLoaded = 64, Off = 128,
    };
    struct Inputs {
        uint32_t timerHz;
        uint64_t timebaseNs;
        bool ch1, ch2, rolling, internal, busLoaded;
    };
    // Timebase index < 2 of the factory table: 10 and 25 ns/div.
    static constexpr bool timebase(uint64_t timebaseNs) { return timebaseNs == 10 || timebaseNs == 25; }
    // One pair uses both converters. Internal captures use their own fixed buffers and layout.
    static constexpr uint32_t shared(const Inputs &in) {
        return (in.ch1 && !in.ch2 ? 0U : uint32_t(Channels)) | (in.internal ? uint32_t(Internal) : 0U);
    }
    // Restrictions on the legacy automatic policy, as distinct from an explicit selection.
    // CH2 on (and so X-Y) keeps the two ordinary channels: an interleave never turns CH2 off itself.
    static constexpr uint32_t capture(const Inputs &in) {
        return shared(in) | (timebase(in.timebaseNs) ? 0U : uint32_t(Timebase)) |
               (in.rolling ? uint32_t(Rolling) : 0U) |
               (in.busLoaded ? uint32_t(BusLoaded) : 0U);
    }
};
struct InterleavePolicy {
    // The factory's interleave needs a non-zero clock setting: here the 280 MHz profile (140 M words/s,
    // 280 MS/s of CH1), the High one. 200 MHz (USB data) and the 360 MHz trial profile never interleave.
    static constexpr uint32_t TimerHz = RunModes[1].coreHz;
    static_assert(RunModes[1].coreHz == 280000000 && RunModes[1].adcDivider == 2, "Interleave runs on the 280 MHz profile");
    static constexpr bool timebase(uint64_t timebaseNs) { return InterleaveConditions::timebase(timebaseNs); }
    // board: the build's Capabilities::adcInterleave.
    static constexpr bool enabled(bool board, uint32_t timerHz, uint64_t timebaseNs, bool ch1, bool ch2, bool rolling,
                                  bool internal, bool busLoaded) {
        return board && timerHz == TimerHz && !InterleaveConditions::capture({timerHz, timebaseNs, ch1, ch2, rolling, internal, busLoaded});
    }
};
// The ZT703S's selectable interleave: on every run mode whose timer the ADC clock divides, at twice
// that profile's ADC rate (200/100 -> 200 MS/s, 280/140 -> 280 MS/s, 360/90 -> 180 MS/s) at the
// profile's ordinary DMA word rate. Not on the HSI fallback (no profile).
struct SelectableInterleave {
    static constexpr bool profile(uint32_t timerHz) {
        for (const auto &mode : RunModes) if (mode.coreHz == timerHz) return true;
        return false;
    }
    static constexpr uint32_t rate(uint32_t timerHz) { return 2 * (timerHz / adcDividerAt(timerHz)); }
    static constexpr uint32_t blockers(bool selected, const InterleaveConditions::Inputs &in) {
        return (selected ? 0U : uint32_t(InterleaveConditions::Off)) | (profile(in.timerHz) ? 0U : uint32_t(InterleaveConditions::Clock)) |
               InterleaveConditions::shared(in);
    }
};
// The build's rule: what keeps a request from interleaving on this board (0: it interleaves).
// `selected`: the user's choice (ZT703S); the ZT706S factory interleave needs none.
struct InterleaveRule {
    static constexpr uint32_t blockers(const board::Capabilities &board, bool selected, const InterleaveConditions::Inputs &in) {
        if (board.interleaveControl()) return SelectableInterleave::blockers(selected, in);
        return InterleaveConditions::Board;
    }
    static constexpr uint32_t blockers(const board::Capabilities &board, InterleaveMode mode, const InterleaveConditions::Inputs &in) {
        if (board.adcInterleave && mode == InterleaveMode::Auto)
            return (in.timerHz == InterleavePolicy::TimerHz ? 0U : uint32_t(InterleaveConditions::Clock)) | InterleaveConditions::capture(in);
        return blockers(board, mode == InterleaveMode::On, in);
    }
};
struct InterleaveResponse {
    static constexpr uint32_t CurveHz = 30000000; // Below it the factory keeps the calibration gains.
    // The factory curve 0804D720, linear through (30, 1.0), (40, 1.0), (46, 1.31), (60, 1.0), (100 MHz,
    // 1.9), each segment from its right node; above 100 MHz the last one goes on. For f >= CurveHz.
    static double curve(uint32_t hz);
    // The factory projector's bounds (08047E28): a gain outside 1..1.99 is drawn with 1.2.
    static constexpr double Fallback = 1.2;
    static double bounded(double gain) { return gain >= 1.0 && gain <= 1.99 ? gain : Fallback; }
    // The absolute display gain of an interleaved CH1 (0 when the factory gain stays: no known
    // frequency, or below CurveHz). It replaces the calibration gain in voltsPerCode only; the ADC
    // zero code stays the factory transfer's at the unchanged offset PWM.
    static double gain(bool known, uint32_t hz) { return known && hz >= CurveHz ? bounded(curve(hz)) : 0; }
};
// 0804F3F0 once per interleaved record: inside (45 800 000, 62 010 000) Hz it is set at once and the
// count of records outside restarts; outside (also no frequency: the factory's estimate is then 0)
// the count rises to 2 and the third consecutive record outside clears it. Leaving the interleave
// clears it at once.
class InterleaveRangeBoost {
public:
    static constexpr uint32_t LowHz = 45800000, HighHz = 62010000; // Exclusive bounds (literals 0804F580/584).
    static constexpr uint8_t Outside = 2;
    static constexpr bool inside(bool known, uint32_t hz) { return known && hz > LowHz && hz < HighHz; }
    void update(bool known, uint32_t hz) {
        if (inside(known, hz)) { outside_ = 0; active_ = true; }
        else if (outside_ < Outside) ++outside_;
        else active_ = false;
    }
    void leave() { active_ = false; outside_ = 0; }
    bool active() const { return active_; }
private:
    bool active_ = false;
    uint8_t outside_ = 0;
};
// The frequency of CH1 one interleaved record shows, as those two rules use it: the record's own
// analysis over a bounded part (MaximumPoints from its start, never a scan of the whole record), and
// the response and range state that follows for the next capture.
class InterleaveTracker {
public:
    static constexpr uint32_t MaximumPoints = 4096;
    // A record captured interleaved; anything else (no interleave) leaves it.
    void observe(bool known, uint32_t hz) {
        boost_.update(known, hz);
        known_ = known; hz_ = known ? hz : 0;
    }
    void leave() { boost_.leave(); known_ = false; hz_ = 0; }
    bool boost() const { return boost_.active(); }
    double gain() const { return InterleaveResponse::gain(known_, hz_); }
    bool known() const { return known_; }
    uint32_t hz() const { return hz_; }
private:
    InterleaveRangeBoost boost_;
    bool known_ = false;
    uint32_t hz_ = 0;
};
} // namespace openosc::scope
