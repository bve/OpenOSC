#pragma once
#include <cstddef>
#include <cstdint>
#include "openosc/instrument/mode.hpp"
#include "openosc/ui/scope/options.hpp"

namespace openosc {
enum class ElementId : uint8_t { Status, Waveform, Measurements, Reading, Unit, Icon, Parameters, Details, Preview, Count };
enum class LayoutPreset : uint8_t { Cards, Compact, Legacy };
inline constexpr const char* ElementNames[] = {"status", "waveform", "measurements", "reading", "unit", "icon",
                                              "parameters", "details", "preview"};
inline constexpr unsigned ElementCount = static_cast<unsigned>(ElementId::Count);
inline constexpr unsigned ThemeInputElementLimit = ElementCount + 1; // Legacy "mode" is accepted and ignored.
inline constexpr unsigned ElementZOrderMax = ElementCount;
inline constexpr const char* ThemeScreenKeys[] = {"scope", "multimeter", "generator"};
static_assert(sizeof ThemeScreenKeys / sizeof *ThemeScreenKeys == InstrumentModeCount);
struct ElementStyle {
    int16_t x = 0, y = 0, width = 0, height = 0;
    uint32_t color = 0xe2e8f0;
    uint8_t font = 12;
    bool visible = false;
    char icon[64]{};
    uint8_t zOrder = 0;
};
struct Theme {
    LayoutPreset layout = LayoutPreset::Cards;
    plot::Options plot;
    uint32_t background = 0x0b1120, foreground = 0xe2e8f0, accent = 0x38bdf8;
    uint32_t grid = 0x243449, channel1 = 0xfacc15, channel2 = 0x38bdf8;
    uint32_t panel = 0x132034, muted = 0x94a3b8, generator = 0xfb923c;
    // MATH in the palette's Green: apart from CH1's yellow, CH2's sky and the reference violet (#184).
    static constexpr uint32_t StockMath = 0x4ade80;
    uint32_t math=StockMath,reference=0xa78bfa,cursor=0xe2e8f0;
    ElementStyle elements[InstrumentModeCount][ElementCount];
    static Theme defaults(LayoutPreset layout = LayoutPreset::Cards);
};
class ThemeParser {
public:
    static constexpr size_t MaxBytes = 10240;
    static bool parse(const char* json, size_t bytes, Theme& output);
};
}
