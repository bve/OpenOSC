#pragma once
#include "openosc/instrument/mode.hpp"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/meter_function.hpp"
#include "openosc/platform/input.hpp"
#include <cstdint>

namespace openosc {
// Settings > Device > Power > Power-on keys: the screen the instrument starts on when AUTO/RANGE,
// HOLD/SAVE or MODE is held with REL while switching on. A target is Off (the saved start screen), the
// Scope, the Generator or a multimeter function (its MeterFunctions index after FirstFunction).
// Files before it: AUTO/RANGE starts Continuity, the other two keys do nothing.
struct PowerOnKeys {
    enum Key : unsigned { AutoRange, HoldSave, Mode, Count }; // Also the order they are tried in.
    static constexpr uint32_t Off = 0, Oscilloscope = 1, Generator = 2, FirstFunction = 3;
    static constexpr uint32_t Options = FirstFunction + MeterFunctionCount;
    static constexpr uint32_t Keys[Count] = {Keypad::AutoRange, Keypad::HoldSave, Keypad::Mode};
    static_assert(Options <= 256, "a target is one byte");
    uint8_t target[Count] = {FirstFunction + unsigned(dmm::Function::Continuity), Off, Off};
    bool valid() const {
        for (uint32_t t : target) if (t >= Options) return false;
        return true;
    }
    static const char *name(uint32_t target) {
        return target == Off ? "Off" : target == Oscilloscope ? "Oscilloscope" : target == Generator ? "Generator"
                                                                                : MeterFunctions[target - FirstFunction].name;
    }
    // The settings.json value: Off, then the InstrumentModeKeys of the Scope and the Generator.
    static const char *key(uint32_t target) {
        return target == Off ? "off" : target == Oscilloscope ? InstrumentModeKeys[unsigned(MeasurementMode::Oscilloscope)]
             : target == Generator ? InstrumentModeKeys[unsigned(MeasurementMode::Generator)] : MeterFunctions[target - FirstFunction].key;
    }
    // The first target that is not Off among the keys held (a Keypad mask), in Key order.
    uint32_t pick(uint32_t keys) const {
        for (unsigned i = 0; i < Count; ++i) if ((keys & Keys[i]) && target[i] != Off) return target[i];
        return Off;
    }
    // As if the user had last been on that screen: the start mode and the multimeter function.
    static void apply(uint32_t target, uint32_t &mode, uint32_t &meterFunction) {
        if (target == Oscilloscope) mode = unsigned(MeasurementMode::Oscilloscope);
        else if (target == Generator) mode = unsigned(MeasurementMode::Generator);
        else if (target >= FirstFunction) { mode = unsigned(MeasurementMode::Multimeter); meterFunction = target - FirstFunction; }
    }
};
} // namespace openosc
