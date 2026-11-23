#include "openosc/application/controller.hpp"

namespace openosc {
void ApplicationController::restoreSettings(uint32_t now) {
    if (shots_.busy()) { view_.notify(Tooltip::Unavailable, "Finish screenshot first",now); return; }
    Preferences defaults;
    defaults.useBuiltinTheme=1; // Preserve the user's theme file; use the built-in layout after restart.
    if (!preferences_.save(defaults)) { view_.notify(Tooltip::Critical, preferences_.error(),now); return; }
    prefs_=defaults; saves_.dropSettings(); saves_.dropTheme();
    confirmAction_=ShutdownAction::Restart;
    requestShutdown(now);
}
} // namespace openosc
