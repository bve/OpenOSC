#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::boot_splash {
enum class Style : uint8_t { Signal, Amber, Daylight, Count };
constexpr bool valid(Style style) { return style < Style::Count; }

struct Image {
    const uint16_t *colors;
    const uint8_t *commands;
    size_t size;
};

Style choose();
void draw(Style style, uint16_t *frame);
} // namespace openosc::boot_splash
