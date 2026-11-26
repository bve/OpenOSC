#pragma once
#include "openosc/settings/preferences.hpp"
#include <cstddef>

namespace openosc {
class PreferencesJson {
public:
    // Compact formatting fits the file with all custom appearances, decoders and Scope
    // shortcuts (4344 bytes with GitHub #171's measure_short_window_counter). Use it when pretty
    // formatting exceeds this limit. Storage uses the shared JSON text lease
    // (JsonWorkspace::TextCapacity, 10241 bytes; preferences_file.cpp asserts it fits).
    static constexpr size_t MaxBytes = 4480;
    // The JSON tree of the largest file: its members/elements (the DMM hold object's 7, the Scope
    // decoder's five protocols, the Bode setup's) and copied strings (keys and values). The longest
    // valid file leaves a fifth of the slots and at least StringReserve string bytes free
    // (settings_layout). 328 * 16 + 3600 = 8848 bytes of tree capacity, unchanged: the ZT703S SRAM4
    // workspace with DAC enabled has no more (GitHub #169's power.wake and #171's key took a slot
    // each of the string bytes).
    static constexpr size_t DocumentSlots = 328, StringBytes = 3600, StringReserve = 512;
    static_assert(DocumentSlots * 16 + StringBytes == 8848, "The tree's capacity stays within the SRAM4 workspace");
    static bool parse(const char* input, size_t bytes, Preferences& output);
    static size_t encode(const Preferences& values, char* output, size_t capacity,bool pretty=true);
};
}
