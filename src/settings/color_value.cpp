#include "openosc/settings/color_value.hpp"
#include <cstring>

namespace openosc {
namespace {
// The DMM's named colours, in their file order (indices and keys are stored in settings).
constexpr NamedColor swatches[ColorCatalog::Swatches] = {
    {"White", "white", 0xf8fafc},   {"Silver", "silver", 0xcbd5e1}, {"Gray", "gray", 0x8b95a5},
    {"Graphite", "graphite", 0x3a4352}, {"Black", "black", 0x000000}, {"Navy", "navy", 0x0b1220},
    {"Amber", "amber", 0xffb238},   {"Orange", "orange", 0xff7a3d}, {"Yellow", "yellow", 0xfde047},
    {"Red", "red", 0xf87171},       {"Green", "green", 0x4ade80},   {"Cyan", "cyan", 0x22d3ee},
    {"Sky", "sky", 0x38bdf8},       {"Blue", "blue", 0x3b82f6},     {"Violet", "violet", 0xa78bfa},
    {"Paper", "paper", 0xe9e3d3}};
constexpr bool same(const char *a, const char *b) { return *a == *b && (!*a || same(a + 1, b + 1)); }
// ColorCatalog::Named follows the table: a look built from it names the colours its keys say.
static_assert(same(swatches[ColorCatalog::White].key, "white") && same(swatches[ColorCatalog::Silver].key, "silver") &&
              same(swatches[ColorCatalog::Gray].key, "gray") && same(swatches[ColorCatalog::Graphite].key, "graphite") &&
              same(swatches[ColorCatalog::Black].key, "black") && same(swatches[ColorCatalog::Navy].key, "navy") &&
              same(swatches[ColorCatalog::Amber].key, "amber") && same(swatches[ColorCatalog::Orange].key, "orange") &&
              same(swatches[ColorCatalog::Yellow].key, "yellow") && same(swatches[ColorCatalog::Red].key, "red") &&
              same(swatches[ColorCatalog::Green].key, "green") && same(swatches[ColorCatalog::Cyan].key, "cyan") &&
              same(swatches[ColorCatalog::Sky].key, "sky") && same(swatches[ColorCatalog::Blue].key, "blue") &&
              same(swatches[ColorCatalog::Violet].key, "violet") && same(swatches[ColorCatalog::Paper].key, "paper"),
              "ColorCatalog::Named follows the swatch table");
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace
uint32_t ColorValue::resolve(uint32_t inherited) const {
    if (index == Custom) return rgb();
    return index < ColorCatalog::Swatches ? swatches[index].rgb : inherited;
}
bool ColorValue::valid() const { return index == Inherit || index == Custom || index < ColorCatalog::Swatches; }
const NamedColor &ColorCatalog::swatch(unsigned index) { return swatches[index < Swatches ? index : 0]; }
uint8_t ColorCatalog::byKey(const char *key) {
    for (unsigned i = 0; key && i < Swatches; ++i) if (!std::strcmp(swatches[i].key, key)) return uint8_t(i);
    return ColorValue::Inherit;
}
ColorValue ColorText::parse(const char *text) {
    if (!text) return {};
    if (text[0] == '#') {
        uint32_t rgb = 0;
        for (unsigned i = 1; i < 7; ++i) {
            const int digit = hex(text[i]);
            if (digit < 0) return {};
            rgb = rgb << 4 | uint32_t(digit);
        }
        return text[7] ? ColorValue{} : ColorValue::custom(rgb);
    }
    return ColorValue{ColorCatalog::byKey(text), 0, 0, 0};
}
bool ColorText::format(const ColorValue &value, char *output, size_t capacity) {
    static constexpr char digits[] = "0123456789abcdef";
    if (!output || !capacity) return false;
    output[0] = 0;
    if (value.isCustom()) {
        if (capacity < 8) return false;
        const uint32_t rgb = value.rgb();
        output[0] = '#';
        for (unsigned i = 0; i < 6; ++i) output[1 + i] = digits[rgb >> (20 - 4 * i) & 0xf];
        output[7] = 0;
        return true;
    }
    if (value.index >= ColorCatalog::Swatches) return false; // Inherit (or invalid): not written.
    const char *key = swatches[value.index].key;
    if (std::strlen(key) >= capacity) return false;
    std::strcpy(output, key);
    return true;
}
bool ColorContrast::light(uint32_t rgb) {
    const uint32_t r = rgb >> 16 & 0xff, g = rgb >> 8 & 0xff, b = rgb & 0xff;
    return 299 * r + 587 * g + 114 * b > 140000; // Luma above ~55 %.
}
uint32_t ColorShade::toward(uint32_t rgb, uint32_t background, unsigned percent) {
    if (percent >= 100) return rgb;
    uint32_t out = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        const int color = int(rgb >> shift & 0xff), base = int(background >> shift & 0xff), d = (color - base) * int(percent);
        out |= uint32_t(base + (d + (d < 0 ? -50 : 50)) / 100) << shift;
    }
    return out;
}
} // namespace openosc
