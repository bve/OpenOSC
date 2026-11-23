#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc {
// One user colour of any appearance (DMM element, Scope role): inherited, one of the shared named
// colours (ColorCatalog), or an exact 0..255 RGB. `index` keeps MeterRoleStyle::color's meaning:
// 0..Swatches-1 a named colour, Inherit (255) the base; Custom (254) adds the r, g, b channels.
struct ColorValue {
    static constexpr uint8_t Inherit = 0xff, Custom = 0xfe;
    uint8_t index = Inherit, r = 0, g = 0, b = 0;
    static constexpr ColorValue named(uint8_t swatch) { return {swatch, 0, 0, 0}; }
    static constexpr ColorValue custom(uint32_t rgb) { return {Custom, uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)}; }
    bool inherit() const { return index == Inherit; }
    bool isCustom() const { return index == Custom; }
    uint32_t rgb() const { return uint32_t(r) << 16 | uint32_t(g) << 8 | b; }
    // The colour shown: the named or custom one, else `inherited`.
    uint32_t resolve(uint32_t inherited) const;
    bool valid() const; // Inherit, Custom or a catalog index.
    bool operator==(const ColorValue &o) const { return index == o.index && (index != Custom || (r == o.r && g == o.g && b == o.b)); }
    bool operator!=(const ColorValue &o) const { return !(*this == o); }
};
static_assert(sizeof(ColorValue) == 4);

// The one table of named colours, shared by the DMM (MeterAppearanceCatalog delegates to it, same
// order and keys as before) and the Scope.
struct NamedColor { const char *name, *key; uint32_t rgb; };
class ColorCatalog {
public:
    static constexpr unsigned Swatches = 16;
    // The table's indices by name, for looks built at compile time (MeterAppearanceCatalog::hybrid).
    enum Named : uint8_t { White, Silver, Gray, Graphite, Black, Navy, Amber, Orange, Yellow, Red, Green, Cyan, Sky, Blue, Violet, Paper };
    static const NamedColor &swatch(unsigned index);
    static uint8_t byKey(const char *key); // ColorValue::Inherit if none.
};

// File text of a colour: a catalog key ("amber") or "#rrggbb" for Custom; Inherit is not written.
// Reading accepts both; anything else is Inherit (an unknown key never fails the file).
struct ColorText {
    static constexpr size_t Capacity = 16; // "#rrggbb" or the longest key ("graphite") + NUL, with room.
    static ColorValue parse(const char *text);
    static bool format(const ColorValue &value, char *output, size_t capacity); // false: Inherit.
};

// Text over a fill: dark ink on a light fill, light ink on a dark one (ITU-R BT.601 luma, integer).
struct ColorContrast {
    static constexpr uint32_t Dark = 0x0b1120, Light = 0xf8fafc;
    static bool light(uint32_t rgb);
    static uint32_t ink(uint32_t fill) { return light(fill) ? Dark : Light; }
};

// A colour dimmed towards a background: percent 100 is the colour itself (unchanged), 0 the
// background, per 8-bit channel and rounded. The same on a dark or a light background.
struct ColorShade {
    static uint32_t toward(uint32_t rgb, uint32_t background, unsigned percent);
};
} // namespace openosc
