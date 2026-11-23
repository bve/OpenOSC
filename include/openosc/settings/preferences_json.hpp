#pragma once
#include "openosc/settings/preferences.hpp"
#include <cstddef>

namespace openosc {
class PreferencesJson {
public:
    // Compact formatting fits the file with all custom appearances, decoders and Scope
    // shortcuts. Use it when pretty formatting exceeds this limit. Storage uses the shared JSON text lease
    // (JsonWorkspace::TextCapacity, 10241 bytes; preferences_file.cpp asserts it fits).
    static constexpr size_t MaxBytes = 4480;
    // The JSON tree of the largest file: its members/elements (the DMM hold object's 7, the Scope
    // decoder's five protocols, the Bode setup's) and copied strings (keys and values). The longest
    // valid file leaves a fifth of the slots and at least StringReserve string bytes free
    // (settings_layout). 330 * 16 + 3568 = 8848 bytes of tree capacity. The AWG sample rate and
    // multimeter.temperature.type take a slot each from the string allowance so the ZT703S
    // workspace uses the same amount of SRAM4.
    static constexpr size_t DocumentSlots = 330, StringBytes = 3568, StringReserve = 512;
    static_assert(DocumentSlots * 16 + StringBytes == 8848, "The tree's capacity stays within the SRAM4 workspace");
    static bool parse(const char* input, size_t bytes, Preferences& output);
    static size_t encode(const Preferences& values, char* output, size_t capacity,bool pretty=true);
};
}
