#pragma once
#include "openosc/settings/color_value.hpp"
#include "openosc/settings/run_mode.hpp"
#include <cstdint>

namespace openosc::ui {
// The Scope top bar's run profile mark (GitHub #47, Settings > Display > Clock indicator): L, M or H
// for RunModes[0..2] (200, 280, 360 MHz), the profile the core runs (ClockControl::activeMode), not
// the frame rate or the sample rate. A 5 x 7 one-bit raster as TagDigits: drawn as opaque runs, the
// same pixels at any position, no antialiasing; the letters differ by shape, colour only adds to it.
struct ProfileGlyph {
    static constexpr int Width = 5, Height = 7;
    static constexpr char Letters[] = {'L', 'M', 'H'};
    // Rows top to bottom; bit Width-1 is the leftmost column.
    static constexpr uint8_t Rows[3][Height] = {
        {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}, // L
        {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}, // M
        {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}, // H
    };
    static constexpr bool shown(uint32_t mode) { return mode < RunModeCount; } // No glyph for the HSI fallback.
    static constexpr bool ink(uint32_t mode, int x, int y) {
        return shown(mode) && x >= 0 && x < Width && y >= 0 && y < Height && (Rows[mode][y] >> (Width - 1 - x)) & 1U;
    }
    // L in the bar's neutral (secondary) ink; M and H, above the factory's 200 MHz, in warning inks
    // of their own: amber and red, deep on a light bar (WCAG >= 4.5 on #f8fafc), bright on a dark one.
    static constexpr uint32_t WarningDark[2] = {0xfbbf24, 0xf87171}, WarningLight[2] = {0xb45309, 0xb91c1c};
    static uint32_t color(uint32_t mode, uint32_t background, uint32_t neutral) {
        if (!mode || !shown(mode)) return neutral;
        return (ColorContrast::light(background) ? WarningLight : WarningDark)[mode - 1];
    }
};
static_assert(RunModeCount == 3, "One letter per run profile: L, M, H");
} // namespace openosc::ui
