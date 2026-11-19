#include "openosc/ui/components/meter_style.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/fonts.hpp"
#include <cstring>

namespace openosc::ui {
const char *MeterStyles::faceName(unsigned face) {
    static constexpr const char *names[] = {"IBM", "Mono", "Segment"};
    return names[face < Faces ? face : unsigned(Ibm)];
}
const char *MeterStyles::paletteName(unsigned scheme) {
    static constexpr const char *names[] = {"Dark", "Amber", "Paper"};
    return names[scheme < Schemes ? scheme : unsigned(Dark)];
}
MeterPalette MeterStyles::palette(unsigned scheme, const Theme &theme) {
    switch (scheme) {
    case Amber: return {0x0c0905, 0xffb238, 0xa0702c, 0xffd27a, 0x3a2a12, 0xff7a3d, 0xff5a4f};
    case Paper: return {0xe9e3d3, 0x1c1c1a, 0x645d50, 0x1f4e8c, 0xb5ab95, 0xc0262d, 0xc0262d};
    default: return {theme.background, theme.foreground, theme.muted, theme.accent, theme.grid, 0xf87171, 0xf87171};
    }
}
bool MeterLook::operator==(const MeterLook &b) const {
    return !std::memcmp(&palette, &b.palette, sizeof palette) && !std::memcmp(colors, b.colors, sizeof colors) &&
           !std::memcmp(fonts, b.fonts, sizeof fonts) && face == b.face && arcWidth == b.arcWidth && tickWidth == b.tickWidth &&
           needleWidth == b.needleWidth;
}
const lv_font_t *MeterLook::text(MeterRole role, unsigned size) const { return MeterStyles::text(fonts[unsigned(role)], size); }
const lv_font_t *MeterStyles::text(unsigned family, unsigned size) {
    if (family != MeterAppearanceCatalog::TextMono) return Elements::font(size);
    switch (size) {
    case 14: return &openosc_mono_14;
    case 24: return &openosc_mono_24;
    case 32: return &openosc_numeric_32;
    case 40: return &openosc_numeric_40;
    default: return &openosc_mono_12;
    }
}
MeterLook MeterStyles::look(unsigned scheme, unsigned face, const MeterAppearance &appearance, const Theme &theme, uint32_t parameters) {
    MeterLook look;
    const auto p = look.palette = palette(scheme, theme);
    look.face = face < Faces ? face : unsigned(Ibm);
    // What each element was drawn in before overrides existed (the summary's texts inherit ink).
    using R = MeterRole;
    const uint32_t base[MeterAppearance::Roles] = {
        p.background, p.ink, p.accent, p.accent,     // Background, Reading, Unit, Function
        p.ink, p.ink, p.ink, p.needle,               // Arc, Ticks, ScaleNumbers, Needle
        p.muted, p.ink, p.ink, p.ink,                // StatNames, Minimum, Maximum, Average
        p.ink, p.ink, p.accent, p.muted, p.accent,   // Range, Secondary, Mode, Details, Bar
        parameters, theme.muted, theme.foreground,   // CardActive, CardInactive, CardText
        parameters};                                 // CardSelection
    for (unsigned i = 0; i < MeterAppearance::Roles; ++i) {
        look.colors[i] = appearance.roles[i].colorValue().resolve(base[i]);
    }
    using C = MeterAppearanceCatalog;
    for (unsigned i = 0; i < MeterAppearance::Roles; ++i) {
        const auto font = appearance.roles[i].font;
        look.fonts[i] = font == MeterRoleStyle::Inherit ? uint8_t(C::TextIbm) : font; // Text: IBM by default, as before.
    }
    const auto reading = appearance[R::Reading].font; // Default: the legacy face (Digits).
    look.face = reading == MeterRoleStyle::Inherit ? look.face : reading;
    look.fonts[unsigned(R::Reading)] = uint8_t(look.face);
    const auto bold = [&](R role) { return appearance[role].style == C::Bold; };
    look.arcWidth = bold(R::Arc) ? 4 : 2;
    look.tickWidth = bold(R::Ticks) ? 2 : 1;
    look.needleWidth = bold(R::Needle) ? 3 : 2;
    return look;
}
ReadingFonts MeterStyles::reading(unsigned face, unsigned size) {
    struct Sized { unsigned size; const lv_font_t *font; };
    // 72 and 80: the large reading (Primary without bar graph and statistics).
    static const Sized faces[Faces][4] = {{{80, &openosc_ibm_80}, {72, &openosc_ibm_72}, {64, &openosc_ibm_64}, {52, &openosc_ibm_52}},
                                          {{80, &openosc_mono_80}, {72, &openosc_mono_72}, {64, &openosc_mono_64}, {56, &openosc_numeric_56}},
                                          {{80, &openosc_segment_80}, {72, &openosc_segment_72}, {64, &openosc_segment_64}, {52, &openosc_segment_52}}};
    static const Sized shared[] = {{40, &openosc_numeric_40}, {32, &openosc_numeric_32}, {24, &openosc_ui_24},
                                   {14, &openosc_ui_14}, {12, &openosc_ui_12}};
    ReadingFonts result;
    const auto add = [&](const Sized &entry) {
        if (entry.font && entry.size <= size && result.count < ReadingFonts::Capacity) result.fonts[result.count++] = entry.font;
    };
    for (const auto &entry : faces[face < Faces ? face : unsigned(Ibm)]) add(entry);
    for (const auto &entry : shared) add(entry);
    if (!result.count) result.fonts[result.count++] = &openosc_ui_12;
    return result;
}
} // namespace openosc::ui
