#pragma once
#include "openosc/application/shutdown_sequence.hpp"
#include <cstdint>

namespace openosc {
// The theme/plot and settings files waiting to be written. A NOR save stalls the main loop for up to
// ~0.7 s, so nothing is written while running: ShutdownSequence (power key, low battery, auto off)
// and the Recovery handoff save once (flush*). A crash or reset loses changes since the last power-off.
class PendingSaves {
public:
    class Store {
    public:
        virtual ~Store() = default;
        virtual bool saveTheme() = 0;
        virtual bool savePreferences() = 0;
    };
    void settingsChanged() { settings_ = true; }
    void plotChanged() { theme_ = true; }
    void dropTheme() { theme_ = false; }
    void dropSettings() { settings_ = false; }
    bool themePending() const { return theme_; }
    bool settingsPending() const { return settings_; }
    // ShutdownSequence's one attempt each: true when nothing was pending or it is saved.
    bool flushTheme(Store &store);
    bool flushSettings(Store &store);

private:
    bool theme_ = false, settings_ = false;
};

class ThemeStore;
// What a theme load says after a reload (nullptr: a plain load).
struct ThemeNotice {
    static const char *text(const ThemeStore &themes);
};
} // namespace openosc
