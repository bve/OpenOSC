#include "openosc/settings/preferences_file.hpp"
#include "openosc/settings/preferences_json.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "openosc/storage/directories.hpp"
#include "openosc/storage/json_files.hpp"
#include <cstring>

namespace openosc {
bool PreferencesFile::readValid(const char* path, Preferences& output, bool& storageError) const {
    static_assert(PreferencesJson::MaxBytes + 1 <= JsonWorkspace::TextCapacity);
    JsonWorkspace::Text json; size_t bytes = 0; // Off the stack (JsonWorkspace).
    if (!json || JsonWorkspace::documentHeld()) { storageError = true; return false; } // Busy, not invalid.
    const auto read = JsonFiles::read(path, json.data(), PreferencesJson::MaxBytes + 1, bytes);
    storageError = read == JsonFiles::ReadResult::StorageError;
    return read == JsonFiles::ReadResult::Complete && PreferencesJson::parse(json.data(), bytes, output);
}
bool PreferencesFile::load() {
    // A busy workspace is unreadable now: nothing changes, nothing is archived or written.
    if (!JsonWorkspace::idle()) { error_ = "CANNOT READ SETTINGS JSON"; repaired_ = restored_ = false; return false; }
    values_ = Preferences{}; error_ = nullptr; repaired_ = restored_ = false;
    if (!fs_.mounted()) { error_ = "SETTINGS DISK IS NOT AVAILABLE"; return false; }
    bool storageError = false;
    Preferences candidate;
    if (readValid(Path, candidate, storageError)) { values_ = candidate; backup(); return true; }
    if (storageError) { error_ = "CANNOT READ SETTINGS JSON"; return false; }
    FILINFO info{};
    if (f_stat(Path, &info) == FR_OK) { // Present but invalid: kept aside, never overwritten.
        if (!JsonFiles::archiveBad(Path)) { error_ = "CANNOT PRESERVE INVALID SETTINGS"; return false; }
        repaired_ = true;
    }
    // settings.json's previous, then SETCOPY and its previous, then the builds 9-12 copy and its previous.
    const char *const files[] = {Path, storage::Layout::SettingsCopy, storage::Layout::SettingsCopyOld};
    char previous[JsonFiles::PathBytes];
    for (unsigned i = 1; i < 6; ++i) {
        const char *const path = !(i & 1) ? files[i / 2] : JsonFiles::previousPath(files[i / 2], previous, sizeof previous) ? previous : nullptr;
        if (!path) continue;
        if (readValid(path, candidate, storageError)) { values_ = candidate; restored_ = true; break; }
        if (storageError) { error_ = "CANNOT READ SETTINGS JSON"; return false; }
    }
    return save(values_);
}
bool PreferencesFile::save(const Preferences& values) {
    error_ = nullptr;
    if (!fs_.mounted() || storage::ensureDirectory(storage::Layout::Settings) != FR_OK) { error_ = "SETTINGS DISK IS NOT AVAILABLE"; return false; }
    {
        JsonWorkspace::Text json; // Off the stack (JsonWorkspace); released before backup() takes it.
        const size_t bytes = json ? PreferencesJson::encode(values, json.data(), PreferencesJson::MaxBytes + 1) : 0;
        if (!bytes || !JsonFiles::write(Path, json.data(), bytes)) { error_ = "CANNOT SAVE SETTINGS JSON"; return false; }
    }
    values_ = values;
    Preferences check; bool storageError = false; // The copy only from a file that reads back valid.
    if (readValid(Path, check, storageError)) backup(); // At the save (power-off), not as a boot-time NOR write.
    return true;
}
// After a save and from settings that loaded clean; written only when it differs.
void PreferencesFile::backup() {
    JsonWorkspace::Text json;
    const size_t bytes = json ? PreferencesJson::encode(values_, json.data(), PreferencesJson::MaxBytes + 1) : 0;
    if (!bytes) return;
    const auto hash = [&](size_t n) { uint64_t h = 1469598103934665603ULL; for (size_t i = 0; i < n; ++i) h = (h ^ uint8_t(json.data()[i])) * 1099511628211ULL; return h; };
    const uint64_t wanted = hash(bytes);
    size_t stored = 0; // The same encoding byte for byte (padding makes a struct compare unreliable).
    const auto read = JsonFiles::read(storage::Layout::SettingsCopy, json.data(), JsonWorkspace::Text::capacity(), stored);
    if (read == JsonFiles::ReadResult::StorageError) return;
    if (read == JsonFiles::ReadResult::Complete && stored == bytes && hash(stored) == wanted) return;
    if (storage::ensureDirectory(storage::Layout::SettingsCopyDirectory) != FR_OK) return;
    if (PreferencesJson::encode(values_, json.data(), PreferencesJson::MaxBytes + 1) == bytes) JsonFiles::write(storage::Layout::SettingsCopy, json.data(), bytes);
}
}
