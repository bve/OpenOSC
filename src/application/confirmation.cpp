#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/confirmation.hpp"

// The confirmation panel every confirmed operation goes through (shutdown, zero, gain, images, waveforms,
// the interleave offset): asked, answered, and left back to where it was asked from.
namespace openosc {
void ApplicationController::confirmOperation(const ui::Confirmation &question,ConfirmHandler handler) {
    confirmationImages_=panel_==Panel::Images;
    confirmationCaptures_=panel_==Panel::Captures; // Its catalog stays allocated until the answer.
    confirmationSessions_=panel_==Panel::Sessions; // Its browser stays allocated until the answer.
    confirmationZero_=panel_==Panel::Zero;
    confirmationSettings_=state_.settingsOpen;
    cancelAuto(HAL_GetTick()); confirmation_=question; confirmationHandler_=handler;
    state_.settingsOpen = false;
    openPanel(Panel::Confirmation, confirmation_); // On Cancel.
}
void ApplicationController::confirmationKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = (keys & Keypad::Mode) ? Outcome::Back : navigator_.keys(keys, confirmation_);
    if (outcome == Outcome::Back || (outcome == Outcome::Activate && navigator_.row() == 0)) { leaveConfirmation(now); return; }
    if (outcome == Outcome::Activate && confirmationHandler_) { (this->*confirmationHandler_)(now); return; }
    view_.properties(confirmation_, navigator_);
}
// Out of the confirmation, back to where it was asked from (the image view, the zero panel while a
// zero still runs, the Settings page on its row, else the instrument), its handler forgotten: Cancel,
// and a handler that continues there (the interleave offset measures under its Settings row).
void ApplicationController::leaveConfirmation(uint32_t now) {
    confirmationHandler_=nullptr;
    if (confirmationImages_) { panel_=Panel::Images; imageShowing_=false; showImages(); }
    else if (confirmationCaptures_ && captures_) { confirmationCaptures_=false; panel_=Panel::Captures; showCaptures(); }
    else if (confirmationSessions_ && sessions_) { confirmationSessions_=false; panel_=Panel::Sessions; showSessions(); }
    else if (confirmationZero_ && data_.zeroBusy()) { panel_=Panel::Zero; zeroProgress_=UINT32_MAX; serviceZeroUi(now); }
    else if (confirmationSettings_) { panel_=Panel::None; state_.settingsOpen=true; navigator_.reset(settingsMenu_.selected()); showSettings(); }
    else closePanel();
}
} // namespace openosc
