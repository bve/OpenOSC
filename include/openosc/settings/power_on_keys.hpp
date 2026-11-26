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
// Kept as the number power.on_keys holds in settings.json (packed): 5 bits a target, AUTO/RANGE
// lowest; absent while all three are at the defaults.
// Files before it: AUTO/RANGE starts Continuity, the other two keys do nothing.
struct PowerOnKeys {
    enum Key : unsigned { AutoRange, HoldSave, Mode, Count }; // Also the order they are tried in.
    static constexpr uint32_t Off = 0, Oscilloscope = 1, Generator = 2, FirstFunction = 3;
    static constexpr uint32_t Options = FirstFunction + MeterFunctionCount;
    static constexpr uint32_t Keys[Count] = {Keypad::AutoRange, Keypad::HoldSave, Keypad::Mode};
    static constexpr unsigned TargetBits = 5;
    static constexpr uint32_t TargetMask = (1u << TargetBits) - 1;
    static_assert(Options <= (1u << TargetBits), "a target is 5 bits of power.on_keys");
    static_assert(Count * TargetBits <= 16, "packed holds the three targets");
    // AUTO/RANGE starts Continuity, HOLD/SAVE and MODE are Off.
    static constexpr uint16_t Defaults = (FirstFunction + unsigned(dmm::Function::Continuity)) << (AutoRange * TargetBits);
    // The three targets, TargetBits each, in Key order from the lowest bits.
    uint16_t packed = Defaults;
    uint32_t target(unsigned key) const { return (packed >> (key * TargetBits)) & TargetMask; }
    void setTarget(unsigned key, uint32_t value) {
        packed = uint16_t((packed & ~(TargetMask << (key * TargetBits))) | (value & TargetMask) << (key * TargetBits));
    }
    // A packed value whose targets are all below Options, with no bit above the three fields.
    static bool valid(uint32_t bits) {
        if (bits >> (Count * TargetBits)) return false;
        for (unsigned i = 0; i < Count; ++i) if (((bits >> (i * TargetBits)) & TargetMask) >= Options) return false;
        return true;
    }
    bool valid() const { return valid(packed); }
    static const char *name(uint32_t target) {
        return target == Off ? "Off" : target == Oscilloscope ? "Oscilloscope" : target == Generator ? "Generator"
                                                                                : MeterFunctions[target - FirstFunction].name;
    }
    // The first target that is not Off among the keys held (a Keypad mask), in Key order.
    uint32_t pick(uint32_t keys) const {
        for (unsigned i = 0; i < Count; ++i) if ((keys & Keys[i]) && target(i) != Off) return target(i);
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
