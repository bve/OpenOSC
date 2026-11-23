#include "openosc/application/controller.hpp"
#include "openosc/ui/editors/meter_properties.hpp"

namespace openosc {
// The DMM hold modes in the controller (docs/DMM_AUTO_HOLD.md): the HOLD key, Measurement > Hold and
// the one path for new settings (the panel, SCPI). The detector itself is dmm::HoldDetector.
void ApplicationController::meterHoldKey(uint32_t now) {
    const auto &meter = data_.meter();
    if (meter.held() || prefs_.meter.hold.mode == dmm::HoldMode::Manual) {
        if (!data_.holdMeter(!meter.held())) view_.notify(Tooltip::Unavailable, "HOLD needs a valid reading", now);
    } else if (meter.holdCycle().active()) data_.cancelMeterHold();
    else if (!data_.armMeterHold()) view_.notify(Tooltip::Unavailable, "HOLD needs the DMM", now);
    dataRefresh_ = true;
}
// Each committed field is saved and applied: the detector takes the new settings (a running cycle and
// its snapshot end; none starts until HOLD). A cancelled number gives back what it began with.
void ApplicationController::meterHoldKeys(uint32_t keys, uint32_t now) {
    if (!keys) return;
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = navigator_.keys(keys, holdEditor_);
    if (outcome == Outcome::Back) {
        meterEditor_ = meterProperties();
        openPanel(Panel::Meter, meterEditor_, ui::MeterProperties::Hold);
        return;
    }
    if (outcome == Outcome::Changed && !meterHoldSettings(holdEditor_.settings(), now)) holdEditor_ = ui::MeterHoldProperties(prefs_.meter.hold);
    view_.properties(holdEditor_, navigator_);
}
bool ApplicationController::meterHoldSettings(const dmm::HoldSettings &settings, uint32_t now) {
    if (!settings.valid()) return false;
    if (settings == prefs_.meter.hold) return true;
    prefs_.meter.hold = settings;
    data_.configureMeterHold(settings);
    changed(now); dataRefresh_ = true;
    return true;
}
} // namespace openosc
