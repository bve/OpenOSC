#pragma once
#include "openosc/measurement/dmm/result.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openosc::dmm {
// How HOLD takes the reading it shows (docs/DMM_AUTO_HOLD.md). Manual: the reading at the key press,
// as always. Delayed: a fixed countdown, then the first valid fresh reading. Stable: armed by HOLD,
// the first stable plateau, once. Auto: armed by HOLD, every new stable plateau after the reading
// has left the last one, with a deadband around zero for voltage, current and capacitance.
enum class HoldMode : uint8_t { Manual, Delayed, Stable, Auto, Count };
// The user's settings, one validation for the editor, the settings file and SCPI. "Digits" are units
// of the last digit the reading shows (its quantum: unit prefix and decimals), never an absolute value
// carried over from another range. rearmDigits > bandDigits always: a plateau must be left by more
// than it may wander.
struct HoldSettings {
    static constexpr uint32_t MinDelayMs = 1000, MaxDelayMs = 60000, DelayStepMs = 1000;
    static constexpr uint32_t MinStableMs = 200, MaxStableMs = 10000, StableStepMs = 100;
    static constexpr uint32_t MinBand = 1, MaxBand = 100, MinRearm = 2, MaxRearm = 1000, MinReadings = 2, MaxReadings = 16;
    static constexpr uint32_t MaxZero = 1000;
    HoldMode mode = HoldMode::Manual;
    uint16_t delayMs = 6000, stableMs = 1000, bandDigits = 5, rearmDigits = 20, minReadings = 3, zeroDigits = 50;
    bool valid() const {
        return unsigned(mode) < unsigned(HoldMode::Count) && delayMs >= MinDelayMs && delayMs <= MaxDelayMs &&
               delayMs % DelayStepMs == 0 && stableMs >= MinStableMs && stableMs <= MaxStableMs && bandDigits >= MinBand &&
               bandDigits <= MaxBand && rearmDigits >= MinRearm && rearmDigits <= MaxRearm && rearmDigits > bandDigits &&
               minReadings >= MinReadings && minReadings <= MaxReadings && zeroDigits <= MaxZero;
    }
    // The linked pair, each within its limits: a band raised to or past the rearm threshold lifts it,
    // a threshold lowered to or below the band lowers the band (rearm > band holds after either).
    void band(uint32_t digits) {
        bandDigits = digits < MinBand ? MinBand : digits > MaxBand ? MaxBand : digits;
        if (rearmDigits <= bandDigits) rearmDigits = bandDigits + 1; // MaxBand + 1 <= MaxRearm.
    }
    void rearm(uint32_t digits) {
        rearmDigits = digits < MinRearm ? MinRearm : digits > MaxRearm ? MaxRearm : digits;
        if (bandDigits >= rearmDigits) bandDigits = rearmDigits - 1; // MinRearm - 1 >= MinBand.
    }
    bool operator==(const HoldSettings &b) const {
        return mode == b.mode && delayMs == b.delayMs && stableMs == b.stableMs && bandDigits == b.bandDigits &&
               rearmDigits == b.rearmDigits && minReadings == b.minReadings && zeroDigits == b.zeroDigits;
    }
    bool operator!=(const HoldSettings &b) const { return !(*this == b); }
    // "Manual", "Delayed", "Stable", "Auto"; the keys "manual" ... "auto" (settings file, SCPI).
    static const char *name(HoldMode mode) {
        static constexpr const char *names[] = {"Manual", "Delayed", "Stable", "Auto"};
        return unsigned(mode) < unsigned(HoldMode::Count) ? names[unsigned(mode)] : "";
    }
    static const char *key(HoldMode mode) {
        static constexpr const char *keys[] = {"manual", "delayed", "stable", "auto"};
        return unsigned(mode) < unsigned(HoldMode::Count) ? keys[unsigned(mode)] : "";
    }
    static bool fromKey(const char *text, HoldMode &mode) {
        for (unsigned i = 0; text && i < unsigned(HoldMode::Count); ++i)
            if (!std::strcmp(text, key(HoldMode(i)))) { mode = HoldMode(i); return true; }
        return false;
    }
};
// The capture side of Delayed/Stable/Auto, apart from the manual HOLD and the DMM's own (packetHold).
// Fed with every decoded packet (observe, before Session's HOLD stop: it never freezes with the
// display) and serviced periodically (the countdown, freshness). No heap, no wait. The plateau is a
// window sliding in time over the last readings (a fixed ring of Capacity): the newest readings
// spanning at least the stable time, so an early reading or a spike leaves it as time passes.
class HoldDetector {
public:
    enum class Phase : uint8_t { Idle, Delay, Waiting, Held };
    static constexpr uint64_t StaleUs = 2000000; // A gap this long between readings breaks a window.
    // Readings a window can hold: 10 s (MaxStableMs) at the controller's ~328 ms is 31 of them.
    static constexpr unsigned Capacity = 64;
    // What decides a sample besides the packet, as of this packet: the local REL, the DMM's own HOLD
    // (a frozen source), the selection of another function still pending.
    struct Context { bool relative = false, sourceHeld = false, awaiting = false; };
    void configure(const HoldSettings &settings) { settings_ = settings; cancel(); }
    const HoldSettings &settings() const { return settings_; }
    bool arm(uint64_t nowUs);  // Delayed/Stable/Auto: the cycle begins (false for Manual).
    void cancel();             // Idle: no cycle, no snapshot.
    // An accepted command: another function selected (the snapshot of the old one goes: Stable and
    // Delayed end, Auto waits for the new one), a range command (a new scale: the window restarts).
    void reselect();
    void restartWindow() { size_ = 0; }
    void observe(const Result &result, const Context &context);
    // Periodic, with or without packets: a source gone quiet breaks the window.
    void service(uint64_t nowUs);
    Phase phase() const { return phase_; }
    bool active() const { return phase_ != Phase::Idle; }
    const Result *captured() const { return captured_ ? &snapshot_ : nullptr; }
    uint32_t captures() const { return captures_; } // Every capture since boot (SYST:DMM:HOLD:STAT?).
    uint32_t remainingMs(uint64_t nowUs) const;     // Delay: left of the countdown.
    // The state pill's word: "DELAY 5", "WAIT", "HOLD", "A-WAIT", "A-HOLD"; empty when idle.
    void indicator(uint64_t nowUs, char *output, size_t capacity) const;

private:
    // The context a sample belongs to: the function, the jack and REL (the measurement), with the unit
    // and decimals shown (the range: its quantum).
    struct Signature {
        unsigned function = ~0U; bool current = false, relative = false; uint8_t decimals = 255; char unit[sizeof Result::unit] = "";
        bool sameMeasurement(const Signature &b) const { return function == b.function && current == b.current && relative == b.relative; }
        bool sameRange(const Signature &b) const;
    };
    struct Point { uint64_t timeUs; int64_t counts; };
    // A sample's signature, its value in counts of its last digit and that digit (SI); false when it
    // cannot be one.
    static bool sample(const Result &result, bool relative, Signature &signature, int64_t &counts, double &quantum);
    void push(const Signature &signature, int64_t counts, uint64_t timeUs);
    bool stable() const; // The window ending at the newest reading is a plateau.
    bool departed(const Result &result, double quantum) const; // Auto: left the snapshot.
    void capture(const Result &result, const Signature &signature, double quantum);
    void drop(); // The snapshot no longer belongs to the measurement shown.
    HoldSettings settings_;
    Phase phase_ = Phase::Idle;
    Result snapshot_;
    Signature snapshotSignature_, windowSignature_;
    double snapshotQuantum_ = 0;
    Point ring_[Capacity]{};
    unsigned head_ = 0, size_ = 0; // ring_[head_] is the newest of size_.
    uint64_t armedUs_ = 0, lastUs_ = 0;
    uint32_t lastSequence_ = 0, captures_ = 0;
    bool captured_ = false;
};
} // namespace openosc::dmm
