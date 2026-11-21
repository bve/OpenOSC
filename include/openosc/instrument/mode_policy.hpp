#pragma once
#include "openosc/instrument/mode.hpp"

namespace openosc {
// Scope -> DMM -> GEN -> Scope: every board has all three (the ZT706S generator is its USART3 module).
// MODE's next instrument; without `generator` it is skipped (from it, the rotation goes on as usual).
constexpr MeasurementMode nextMode(MeasurementMode mode, bool generator = true) {
    const auto next = static_cast<MeasurementMode>((static_cast<unsigned>(mode) + 1) % InstrumentModeCount);
    return next == MeasurementMode::Generator && !generator ? nextMode(next) : next;
}
} // namespace openosc
