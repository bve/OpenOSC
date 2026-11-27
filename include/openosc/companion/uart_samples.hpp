#pragma once
#include "openosc/companion/protocol.hpp"

namespace openosc::companion {
class UartSamples {
public:
    static constexpr unsigned PerBit = 4;
    // Captured from the high byte of GPIOA IDR. The ESP drives noninverted 8N2.
    static bool decode(const uint8_t *samples, unsigned size, uint8_t pin, Frame &reply) {
        constexpr unsigned Center = PerBit / 2;
        constexpr unsigned FirstStop = 9 * PerBit + Center;
        constexpr unsigned LastStop = FirstStop + (StopBits - 1) * PerBit;
        Wire wire;
        unsigned at = 1;
        while (at + LastStop < size) {
            if (!(samples[at - 1] & pin) || (samples[at] & pin) || (samples[at + Center] & pin)) {
                ++at;
                continue;
            }
            if (!(samples[at + FirstStop] & samples[at + LastStop] & pin)) {
                wire.reset();
                ++at;
                continue;
            }
            uint8_t byte = 0;
            for (unsigned bit = 0; bit < 8; ++bit)
                if (samples[at + (1 + bit) * PerBit + Center] & pin) byte |= 1u << bit;
            if (wire.feed(byte, reply)) return true;
            at += FirstStop + 1; // Start looking again before the next byte, even with clock skew.
        }
        return false;
    }
};
} // namespace openosc::companion
