#pragma once
#include "openosc/settings/preferences.hpp"
#include <cstddef>

namespace openosc {
class PreferencesJson {
public:
    // Compact formatting fits the longest valid file, with all custom appearances, decoders and
    // Scope shortcuts: 4500 bytes on ZT703S, 4502 on ZT706S, whose generator frequency and AWG
    // sample rate take a digit more each. Of them the Scope's dmm_measurements away from their
    // defaults take 33 and the Scope appearance's DMM colour role 17. Use it when pretty formatting
    // exceeds this limit. Storage uses the shared JSON text lease (JsonWorkspace::TextCapacity,
    // 10241 bytes; preferences_file.cpp asserts it fits).
    static constexpr size_t MaxBytes = 4504;
    // The JSON tree of the largest file: a 16-byte slot per object member or array element (the DMM
    // hold object's 7, the Scope decoder's five protocols, the Bode setup's) and the strings it
    // copies, keys and values, each distinct one once. The longest valid file leaves a fifth of the
    // slots and at least StringReserve string bytes free (settings_layout). 337 * 16 + 3456 = 8848
    // bytes of tree capacity, unchanged: the ZT703S SRAM4 workspace with DAC enabled has no more.
    // The slots come from the string bytes: GitHub #169's power.wake, #171's key and the AWG sample
    // rate took one each; the Scope's dmm_measurements (its key and four readouts) and the DMM
    // colour role take six more than the 263 that 329 slots left room for, and 337 keep a fifth of
    // those 269 free. With every appearance colour distinct the strings take 2581 bytes, 875 free.
    static constexpr size_t DocumentSlots = 337, StringBytes = 3456, StringReserve = 512;
    static_assert(DocumentSlots * 16 + StringBytes == 8848, "The tree's capacity stays within the SRAM4 workspace");
    static bool parse(const char* input, size_t bytes, Preferences& output);
    static size_t encode(const Preferences& values, char* output, size_t capacity,bool pretty=true);
};
}
