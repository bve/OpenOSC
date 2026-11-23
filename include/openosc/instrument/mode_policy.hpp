#pragma once
#include "openosc/instrument/mode.hpp"

namespace openosc {
// Scope -> DMM -> GEN -> Scope: every board has all three (the ZT706S generator is its USART3 module).
constexpr MeasurementMode nextMode(MeasurementMode mode) {
    return static_cast<MeasurementMode>((static_cast<unsigned>(mode) + 1) % InstrumentModeCount);
}
// MODE held: Scope <-> GEN; from the DMM, Scope first.
constexpr MeasurementMode heldMode(MeasurementMode mode) {
    return mode == MeasurementMode::Oscilloscope ? MeasurementMode::Generator : MeasurementMode::Oscilloscope;
}
} // namespace openosc
