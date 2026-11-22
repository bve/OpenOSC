#pragma once
#include "openosc/input/gestures.hpp"
#include <cstdint>

namespace openosc {
// AUTO on the Scope waveform screen (issue #96), from KeyGestures' own KEY5 events: a qualified press
// (PressQualifyMs held, so a bounce never counts) opens a stroke while the screen has the key. Its
// release before PressWindowMs is the tap; a later release does nothing (the countdown showed); the
// first hold (PressHoldMs) is the hold and ends the stroke, so nothing else follows it. A stroke
// the screen did not take (another screen, a panel, a wake) or lost (cancel) gives nothing.
// A stroke that qualified during Auto Setup only cancels it: its countdown stays hidden, so any
// release before the hold is Cancel, and its hold gives nothing, also once the setup has ended.
class ScopeAutoKey {
public:
    enum class Action : uint8_t { None, Tap, Hold, Cancel };
    // One pass's events; `screen`: the waveform screen has AUTO now (else the open stroke ends);
    // `busy`: Auto Setup runs now (latched by the stroke's qualified press).
    Action events(const KeyEvents &events, uint32_t now, bool screen, bool busy);
    void cancel() { open_ = false; } // REL took the stroke, or the screen lost the key.
    bool open() const { return open_; }
    // The countdown of an open idle stroke towards its hold: 0..100 from PressWindowMs, -1 before or without one.
    int percent(uint32_t now) const;
private:
    uint32_t pressedAt_ = 0; // The raw press edge (the press qualifies PressQualifyMs after it).
    bool open_ = false;
    bool cancelling_ = false; // The open stroke qualified during Auto Setup.
};
} // namespace openosc
