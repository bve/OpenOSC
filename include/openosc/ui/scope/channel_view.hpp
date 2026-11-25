#pragma once

namespace openosc::plot {
// Settings > Display > Swap CH1/CH2: which on-screen slot shows each input in the Scope view. A slot
// carries the label, number, colour and footer place; the input keeps its samples, ranges, probe,
// coupling, visibility, offsets, trigger, measurements and every function's routing (X-Y, FFT, math,
// decoders). `input` and `slot` are 0 for CH1 and 1 for CH2.
struct ChannelView {
    bool swapped = false;
    constexpr unsigned slot(unsigned input) const { return (input & 1U) ^ (swapped ? 1U : 0U); }
    constexpr unsigned input(unsigned slot) const { return (slot & 1U) ^ (swapped ? 1U : 0U); }
    constexpr unsigned number(unsigned input) const { return slot(input) + 1; }
    constexpr const char *inputLabel(unsigned input) const {
        if (swapped) return input ? "CH1 (input 2)" : "CH2 (input 1)";
        return input ? "CH2" : "CH1";
    }
    constexpr bool operator==(const ChannelView &other) const { return swapped == other.swapped; }
    constexpr bool operator!=(const ChannelView &other) const { return swapped != other.swapped; }
};
} // namespace openosc::plot
