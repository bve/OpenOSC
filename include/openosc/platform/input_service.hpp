#pragma once
#include "openosc/input/key_input.hpp"

namespace openosc {
class InputService {
public:
    static void enable();
    static void tick(uint32_t now);
    static uint32_t keys();
    static bool activity();
    static bool take(TimedKeyEvents &event);
    static bool overflowed();
    static void suppress();
    static void wake();                       // suppress(), but a REL/Power hold goes on (KeyInput::wake).
    static bool powerHold(uint32_t &elapsed);
    // Holds `key` (Keypad bits) down for `ms` as if it were pressed, through the same sampling as the
    // physical keys. False while an earlier one is still held or its events are not taken (injecting).
    static bool inject(uint32_t key, uint32_t ms);
    static bool injecting();
};
} // namespace openosc
