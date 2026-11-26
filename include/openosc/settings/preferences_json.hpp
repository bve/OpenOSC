#pragma once
#include "openosc/settings/preferences.hpp"
#include <cstddef>

namespace openosc {
class PreferencesJson {
public:
    // Compact formatting fits the file with all custom appearances, decoders and Scope
    // shortcuts. Use it when pretty formatting exceeds this limit. Storage uses the shared JSON buffer.
    static constexpr size_t MaxBytes = 4352;
    // The JSON tree of the largest file: its members/elements (the DMM hold object's 7, the Scope
    // decoder's five protocols, the Bode setup's) and copied strings (keys and values). The longest
    // valid file leaves a fifth of the slots and a sixth of the string bytes free (settings_layout).
    // 326 * 16 + 3632 = 8848 bytes of tree capacity fits the ZT703S SRAM4 workspace with DAC enabled.
    static constexpr size_t DocumentSlots = 326, StringBytes = 3632;
    static bool parse(const char* input, size_t bytes, Preferences& output);
    static size_t encode(const Preferences& values, char* output, size_t capacity,bool pretty=true);
};
}
