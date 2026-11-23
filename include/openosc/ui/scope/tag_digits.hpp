#pragma once
#include "openosc/ui/scope/plot_frame.hpp"
#include <cstdint>

namespace openosc::plot {
// The channel digit of a zero tag as a 1-bit raster. The 9 x 11 tag has no room for a font glyph:
// IBM Plex 12's 7 x 9 ink with its antialiased one-pixel stems sat flush on the box's top and right
// edges and smeared into strokes. A 5 x 7 raster sits centred with two pixels on every side and
// is drawn as opaque runs, so it is the same pixels at any position and under any clip.
struct TagDigits {
    static constexpr int Width = 5, Height = 7;
    static constexpr int Left = (PlotFrame::TagWidth - Width) / 2, Top = (PlotFrame::TagHeight - Height) / 2;
    // Rows top to bottom; bit Width-1 is the leftmost column. Index: channel (0: "1", 1: "2"), 2: the
    // DMM trace ("D").
    static constexpr unsigned Glyphs = 3;
    static constexpr uint8_t Rows[Glyphs][Height] = {
        {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},
        {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},
        {0b11110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b11110},
    };
    static constexpr bool ink(unsigned channel, int x, int y) {
        return channel < Glyphs && x >= 0 && x < Width && y >= 0 && y < Height && (Rows[channel][y] >> (Width - 1 - x)) & 1U;
    }
};
static_assert((PlotFrame::TagWidth - TagDigits::Width) % 2 == 0 && (PlotFrame::TagHeight - TagDigits::Height) % 2 == 0,
              "The digit is centred in its tag");
static_assert(TagDigits::Left >= 2 && TagDigits::Top >= 2, "Two pixels of the tag around the digit");
} // namespace openosc::plot
