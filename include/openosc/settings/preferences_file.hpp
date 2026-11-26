#pragma once
#include "openosc/settings/preferences.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/storage/filesystem.hpp"

namespace openosc {
// The settings: only /openosc/SETTINGS/settings.json, written atomically by JsonFiles::write (verified
// temporary, the replaced file kept as settings.previous.json). No raw flash area: the SPI flash holds
// one FAT superfloppy and nothing outside its files.
class PreferencesFile {
public:
    explicit PreferencesFile(Filesystem& fs) : fs_(fs) {}
    // The JSON; when it is missing or invalid (archived as settings.bad.NNNN.json), the valid previous
    // copy, else the defaults, written as the new JSON. A read error keeps the defaults and writes nothing.
    bool load();
    bool save(const Preferences& values); // False only when the JSON was not written.
    const Preferences& values() const { return values_; }
    // The start mode the power-on keys held (Keypad::read()) pick over the saved one.
    uint32_t startMode(uint32_t keys) const {
        uint32_t mode = values_.mode, function = values_.meterFunction;
        PowerOnKeys::apply(values_.powerOnKeys.pick(keys), mode, function);
        return mode;
    }
    // Applies that start screen. `loaded`: load()'s result (the settings were read, or repaired and written);
    // after a failed load() the screen still changes but is not marked for saving, so the defaults do not
    // replace settings that could not be read.
    void applyPowerOnKeys(uint32_t keys, bool loaded) {
        const uint32_t mode = values_.mode, function = values_.meterFunction;
        PowerOnKeys::apply(values_.powerOnKeys.pick(keys), values_.mode, values_.meterFunction);
        keyStart_ = loaded && (values_.mode != mode || values_.meterFunction != function);
    }
    // A power-on key changed the start screen of settings that were read: saved at power-off like a mode
    // change (nothing written at boot).
    bool keyStart() const { return keyStart_; }
    const char* error() const { return error_; }
    bool repaired() const { return repaired_; } // An invalid JSON was archived.
    bool restored() const { return restored_; } // The values came from a previous or backup copy.
private:
    static constexpr const char* Path = storage::Layout::SettingsFile;
    bool readValid(const char* path, Preferences& output, bool& storageError) const;
    void backup();
    Filesystem& fs_;
    Preferences values_;
    const char* error_ = nullptr;
    bool repaired_ = false, restored_ = false, keyStart_ = false;
};
}
