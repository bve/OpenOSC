#pragma once
#include "openosc/input/gestures.hpp"
#include <cstdint>

namespace openosc {
// The REL/Power hold that powers off, as the factory's KEY4 (703S poll 080459F4 / 706S 08046390):
// the progress box shows after the first 500 ms interval (KeyGestures::PowerCommitMs, where a release
// stops being REL) and fills until the fourth (~2 s), where the long press powers off. Released in
// between: nothing happens.
struct PowerHold {
    static constexpr uint32_t ShowMs = KeyGestures::PowerCommitMs, OffMs = KeyGestures::LongHoldMs;
    static_assert(OffMs > ShowMs);
    // 0..100 while shown, -1 when not. off: the hold setting (Preferences::holdMs).
    static int percent(bool holding, uint32_t elapsed, uint32_t off = OffMs) {
        if (!holding || elapsed < ShowMs) return -1;
        return elapsed >= off ? 100 : int((elapsed - ShowMs) * 100 / (off - ShowMs));
    }
};
} // namespace openosc
