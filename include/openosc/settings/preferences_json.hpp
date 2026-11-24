#pragma once
#include "openosc/settings/preferences.hpp"
#include <cstddef>

namespace openosc {
class PreferencesJson {
public:
    // The whole file fits in 4 KiB with compact formatting, including the widest settings
    // that use custom appearances, decoders and Scope shortcuts. Use compact when pretty does not fit.
    static constexpr size_t MaxBytes = 4096;
    // The JSON tree of the largest file: its members/elements (the DMM hold object's 7, the Scope
    // decoder's five protocols, the Bode setup's) and copied strings (keys and values). The longest
    // valid file leaves at least a fifth of both budgets free (settings_layout). The workspace in
    // SRAM4 fits 320 * 16 + 3664 = 8784 bytes on the MCU; a 64-bit host uses larger slots.
    static constexpr size_t DocumentSlots = 320, StringBytes = 3664;
    static bool parse(const char* input, size_t bytes, Preferences& output);
    static size_t encode(const Preferences& values, char* output, size_t capacity,bool pretty=true);
};
}
