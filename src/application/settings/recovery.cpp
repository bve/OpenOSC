#include "openosc/application/controller.hpp"

namespace openosc {
void ApplicationController::recoveryFromSettings(uint32_t) { recoveryInstall_[0] = 0; confirmShutdown(ShutdownAction::Recovery); }
void ApplicationController::serviceRecovery(uint32_t now) {
    if (!recoveryPending_ || int32_t(now-recoveryAt_)<0) return;
    recoveryPending_=false;
    confirmAction_=ShutdownAction::Recovery; requestShutdown(now);
}
} // namespace openosc
