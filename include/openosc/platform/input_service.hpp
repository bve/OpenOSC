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
    static void wake(bool relativeHold);      // suppress(); with relativeHold a REL/Power hold goes on (KeyInput::wake).
    static bool powerHold(uint32_t &elapsed);
};
} // namespace openosc
