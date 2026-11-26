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
// Stored as one number, power.on_keys in settings.json (packed()): 5 bits a target, AUTO/RANGE lowest;
// absent while all three are at the defaults.
// Files before it: AUTO/RANGE starts Continuity, the other two keys do nothing.
struct PowerOnKeys {
    enum Key : unsigned { AutoRange, HoldSave, Mode, Count }; // Also the order they are tried in.
    static constexpr uint32_t Off = 0, Oscilloscope = 1, Generator = 2, FirstFunction = 3;
    static constexpr uint32_t Options = FirstFunction + MeterFunctionCount;
    static constexpr uint32_t Keys[Count] = {Keypad::AutoRange, Keypad::HoldSave, Keypad::Mode};
    static constexpr unsigned TargetBits = 5;
    static_assert(Options <= (1u << TargetBits), "a target is 5 bits of power.on_keys");
    uint8_t target[Count] = {FirstFunction + unsigned(dmm::Function::Continuity), Off, Off};
    bool valid() const {
        for (uint32_t t : target) if (t >= Options) return false;
        return true;
    }
    static const char *name(uint32_t target) {
        return target == Off ? "Off" : target == Oscilloscope ? "Oscilloscope" : target == Generator ? "Generator"
                                                                                : MeterFunctions[target - FirstFunction].name;
    }
    // The power.on_keys value: the three targets, TargetBits each, in Key order from the lowest bits.
    uint32_t packed() const {
        uint32_t bits = 0;
        for (unsigned i = 0; i < Count; ++i) bits |= uint32_t(target[i]) << (i * TargetBits);
        return bits;
    }
    // Takes a packed value; false (nothing changed) for a target of Options or above or a bit above the three fields.
    bool unpack(uint32_t bits) {
        if (bits >> (Count * TargetBits)) return false;
        uint8_t parsed[Count];
        for (unsigned i = 0; i < Count; ++i) {
            const uint32_t t = (bits >> (i * TargetBits)) & ((1u << TargetBits) - 1);
            if (t >= Options) return false;
            parsed[i] = uint8_t(t);
        }
        for (unsigned i = 0; i < Count; ++i) target[i] = parsed[i];
        return true;
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
