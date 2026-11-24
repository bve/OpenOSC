#include "openosc/settings/instrument_preferences.hpp"
#include "openosc/settings/run_mode.hpp"
#include "openosc/acquisition/measurements.hpp"
#include "openosc/acquisition/probe_attenuation.hpp"

namespace openosc {
bool ScopePreferences::valid() const {
    if (!scope::RecordBudget::valid(recordPoints) || (!enabled[0] && !enabled[1]) ||
        triggerSource > 1 || triggerEdge > 1 || triggerMode > 2 || triggerPosition>1000 || triggerReject>1 ||
        triggerType > 3 || triggerCondition > 4 ||
        triggerMinimumNs < scope::TriggerSettings::MinimumNs || triggerMinimumNs > scope::TriggerSettings::MaximumNs ||
        triggerMaximumNs < scope::TriggerSettings::MinimumNs || triggerMaximumNs > scope::TriggerSettings::MaximumNs ||
        !measurements.valid() || !meterMeasurements.valid() ||
        runMode>=RunModeCount || rollAuto>1 || trend>1 || longerRecord > 1 || interleave>(board::Current::Features.interleaveControl() ? 2U : 1U) ||
        factoryRecordFormat>3 || !arrangement.valid() || !appearance.valid() || !decoder.valid() || !keys.valid()) return false;
    for (unsigned c = 0; c < 2; ++c)
        if (enabled[c] > 1 || coupling[c] > 1 || position[c] > 160 || !probeNumerator[c] ||
            probeNumerator[c] > scope::ProbeAttenuation::MaximumNumerator || !probeDenominator[c] || probeDenominator[c] > 1000) return false;
    return true;
}
} // namespace openosc
