#include "openosc/instrument/data_access.hpp"

namespace openosc {
bool DataAccess::serialMeter(bool enabled) {
    if (!enabled) return serial_.stop();
    if (!active_ || stopping_ || measurement_!=MeasurementMode::Multimeter) return false;
    generator_.reference(false); // The jack goes to the serial output (Scope only: a guard, never shared).
    return serial_.start(meter_.latest().sequence);
}
} // namespace openosc
