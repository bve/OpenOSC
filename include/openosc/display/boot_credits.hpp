#pragma once
#include <cstdint>

namespace openosc::boot_splash {
struct CreditGroup {
    const uint16_t *starts;
    const uint16_t *positions; // Pixel offsets from the top left of a slot.
    const uint8_t *colors;
    const uint16_t (*palette)[2]; // Colors baked for the even and odd row backgrounds.
    uint16_t origin;
    uint8_t count;
};

constexpr uint32_t scramble(uint32_t word) {
    word ^= word << 13;
    word ^= word >> 17;
    return word ^ (word << 5);
}

template<unsigned Slots>
void drawCredits(const CreditGroup &group, uint16_t *frame, uint32_t &seed,
                 unsigned columns, unsigned columnWidth, unsigned rowStride) {
    uint8_t order[Slots];
    for (unsigned i = 0; i < group.count; ++i) order[i] = i;
    for (unsigned slot = 0; slot < group.count; ++slot) {
        // Pick one of the remaining names and replace it with the last one (Fisher–Yates).
        const unsigned remaining = group.count - slot;
        seed = scramble(seed);
        const unsigned pick = (uint64_t(seed) * remaining) >> 32;
        const unsigned name = order[pick];
        order[pick] = order[remaining - 1];
        const unsigned row = slot / columns;
        auto *cell = frame + group.origin + row * rowStride + (slot % columns) * columnWidth;
        for (unsigned i = group.starts[name]; i < group.starts[name + 1]; ++i)
            cell[group.positions[i]] = group.palette[group.colors[i]][row & 1];
    }
}
} // namespace openosc::boot_splash
