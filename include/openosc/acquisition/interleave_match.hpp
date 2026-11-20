#pragma once
#include <cstdint>

namespace openosc::scope {
// The centered Q15 FIR estimates B - A from the combined stream. Subtract it
// from the even (B) samples so the result uses ADC A's scale and sample grid.
struct InterleaveMatch {
    static constexpr unsigned Radius = 4, Taps = 2 * Radius + 1;
    static constexpr int32_t Unity = 32768, MaximumNorm = Unity / 4;
    alignas(4) int16_t q15[Taps]{};

    bool valid() const {
        int32_t norm = 0;
        for (const int32_t tap : q15) norm += tap < 0 ? -tap : tap;
        return norm > 0 && norm <= MaximumNorm;
    }
    int32_t bias(int32_t evenShiftQ8, int32_t zeroQ8) const {
        int32_t result = 0;
        for (unsigned i = 0; i < Taps; ++i)
            result += q15[i] * ((i & 1 ? 0 : evenShiftQ8) - zeroQ8);
        return result;
    }
};
} // namespace openosc::scope
