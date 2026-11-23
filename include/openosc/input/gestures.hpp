#pragma once
#include <cstdint>

namespace openosc {
struct KeyEvents {
    // Keypad masks (13 keys, bits 0..12): 16 bits each, so KeyInput's queue of 128 stays at its size.
    using Mask = uint16_t;
    Mask shortPress = 0;
    Mask longPress = 0;
    Mask repeat = 0;
    // press: AUTO/RANGE (KeyGestures::PressKeys) held PressQualifyMs since its raw press edge, once per
    // stroke, not for a stroke a wake took: the factory's slot1, the only non-empty one (KEY5: 20 20).
    // Its release still gives short/long.
    Mask press = 0;
    // The factory's KEY5 after its qualified press (703S 080459F4 / 706S 08046390, from the raw press
    // edge): release, the key let go before PressWindowMs (its slot0: 21 21); releaseLate, let go from
    // PressWindowMs on (the factory's long flag suppresses that release: nothing is sent, the stroke
    // only ends); holdRepeat, held to PressHoldMs and every PressHoldMs after it while held (slot2,
    // 22 22). Only for a qualified press (PressKeys); exactly one of release/releaseLate per stroke.
    Mask release = 0, releaseLate = 0, holdRepeat = 0;
    bool any() const { return shortPress | longPress | repeat | press | release | releaseLate | holdRepeat; }
};
class KeyGestures {
public:
    static constexpr uint32_t DebounceMs = 25, HoldMs = 1000, PressQualifyMs = 50, PressKeys = 1U << 11; // Keypad::AutoRange.
    // REL/Power and AUTO/RANGE hold as the factory's KEY4/KEY5: four 500 ms intervals (703S 080459F4).
    static constexpr uint32_t LongHoldMs = 2000;
    // REL/Power: from the first interval (500 ms after the debounced press) the power-off progress
    // shows (the hold settings), and a release from then on cancels it with no REL (as the factory's
    // poll); a release before it is REL, or the power off with the short-press setting. AUTO/RANGE
    // keeps its short press below LongHoldMs.
    static constexpr uint32_t PowerCommitMs = 500;
    static constexpr uint32_t RepeatDelayMs = 400, RepeatPeriodMs = 80;
    // The factory's KEY5 intervals: the long flag at the first 500 ms interval, slot2 at the fourth
    // (2 s) and every fourth after it (PressKeys only; the generic short/long above are unchanged).
    static constexpr uint32_t PressWindowMs = 500, PressHoldMs = 2000;
    KeyEvents poll(uint32_t keys, uint32_t now);
    // A non-arrow key held towards its long press (pressed after start, not yet emitted): the time
    // since its debounced press. False otherwise (released, already long, held since before start).
    bool holding(uint32_t bit, uint32_t now, uint32_t &elapsed) const;
    // After a wake: `bit`, if held, still counts towards its long press from `now`, but its release
    // before that gives no short press (the wake took this stroke).
    void holdOnly(uint32_t bit, uint32_t now);
private:
    struct Button {
        uint32_t changedAt = 0, pressedAt = 0, repeatAt = 0, qualifyAt = 0, rawAt = 0, holdAt = 0;
        bool candidate = false, down = false, armed = false, emitted = false, shortless = false, qualified = false;
    } buttons_[13];
    static unsigned index(uint32_t bit) { unsigned i = 0; while (i < 31 && !(bit & (1U << i))) ++i; return i; }
    bool initialized_ = false;
};
}
