#include "openosc/application/scope_auto_key.hpp"
#include "openosc/application/power_hold.hpp"

namespace openosc {
static_assert(PowerHold::ShowMs == KeyGestures::PressWindowMs, "the countdown shows when a release stops being a tap");
ScopeAutoKey::Action ScopeAutoKey::events(const KeyEvents &events, uint32_t now, bool screen, bool busy) {
    constexpr auto Key = KeyGestures::PressKeys; // AUTO/RANGE.
    if (!screen) open_ = false;
    if (events.press & Key) { open_ = screen; cancelling_ = busy; pressedAt_ = now - KeyGestures::PressQualifyMs; }
    if (!open_) return Action::None;
    if (events.holdRepeat & Key) { open_ = false; return cancelling_ ? Action::None : Action::Hold; }
    if (events.release & Key) { open_ = false; return cancelling_ ? Action::Cancel : Action::Tap; }
    if (events.releaseLate & Key) { open_ = false; return cancelling_ ? Action::Cancel : Action::None; }
    return Action::None;
}
int ScopeAutoKey::percent(uint32_t now) const {
    return open_ && !cancelling_ ? PowerHold::percent(true, now - pressedAt_, KeyGestures::PressHoldMs) : -1;
}
} // namespace openosc
