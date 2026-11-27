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
    // Keep a fifth of the slots and at least StringReserve string bytes free in the
    // longest valid settings file (settings_layout), within the existing shared JSON workspace.
    static constexpr size_t DocumentSlots = 332, StringBytes = 3536, StringReserve = 448;
    static bool parse(const char* input, size_t bytes, Preferences& output);
    static size_t encode(const Preferences& values, char* output, size_t capacity,bool pretty=true);
};
}
