#include "openosc/application/pending_saves.hpp"
#include "openosc/settings/theme_store.hpp"

namespace openosc {
bool PendingSaves::flushTheme(Store &store) {
    if (theme_ && store.saveTheme()) theme_ = false;
    return !theme_;
}
bool PendingSaves::flushSettings(Store &store) {
    if (settings_ && store.savePreferences()) settings_ = false;
    return !settings_;
}
const char *ThemeNotice::text(const ThemeStore &themes) {
    if (themes.repaired()) return themes.restored() ? "Bad theme archived; previous copy restored" : "Bad theme archived; defaults restored";
    return themes.restored() ? "Theme restored from previous copy" : nullptr;
}
} // namespace openosc
