#include "openosc/settings/instrument_preferences.hpp"
#include "openosc/settings/run_mode.hpp"
#include "openosc/acquisition/measurements.hpp"

namespace openosc {
bool ScopePreferences::valid() const {
    if (!scope::RecordBudget::valid(recordPoints) || (!enabled[0] && !enabled[1]) ||
        triggerSource > 1 || triggerEdge > 1 || triggerMode > 2 || triggerLevel < -128 || triggerLevel > 127 || triggerPosition>1000 || triggerReject>1 ||
        !measurements.valid() ||
        runMode>=RunModeCount || rollAuto>1 || interleave>1 || factoryRecordFormat>2 || !arrangement.valid() || !appearance.valid() || !decoder.valid()) return false;
    for (unsigned c = 0; c < 2; ++c)
        if (enabled[c] > 1 || coupling[c] > 1 || position[c] > 160 || !probeNumerator[c] ||
            probeNumerator[c] > 1000 || !probeDenominator[c] || probeDenominator[c] > 1000) return false;
    return true;
}
} // namespace openosc
