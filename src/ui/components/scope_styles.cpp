#include "openosc/ui/components/scope_styles.hpp"
#include <cstring>

namespace openosc::ui {
namespace {
using R = ScopeRole;
constexpr unsigned SchemeRoles = unsigned(R::Selection) + 1; // Background .. Selection; the rest derive.
// Each scheme's own colours, Background .. Selection (Trigger is always the source's colour).
// Daylight keeps text, cursors and channels deep enough for a light body (WCAG >= 4.5 on text).
// The DMM trace's colour stands apart from both channels, Math, Reference and Selection.
constexpr uint32_t schemes[unsigned(ScopeScheme::Count) - 1][SchemeRoles] = {
    // High contrast
    {0x000000, 0x141414, 0xffffff, 0xc8c8c8, 0x404040, 0xffff00, 0x00e5ff, 0xff4dff, 0xb8b8ff, 0xff8c1a, 0xffffff, 0, 0x00ff9c},
    // Daylight
    {0xf8fafc, 0xe2e8f0, 0x0f172a, 0x475569, 0xcbd5e1, 0xb45309, 0x0369a1, 0x047857, 0x6d28d9, 0xbe123c, 0x0f172a, 0, 0x1d4ed8},
    // Amber
    {0x0d0800, 0x1f1405, 0xffc46b, 0xa87a3a, 0x3d2a10, 0xffb238, 0xff6b3d, 0xfde047, 0xe9e3d3, 0xa3e635, 0xfff1d6, 0, 0xffd27a}};
// Current: the Theme has no DMM colour (its JSON no key for it); ColorCatalog's Green.
constexpr uint32_t CurrentMeter = 0x4ade80;
uint32_t themeColor(const Theme &t, R role) {
    switch (role) {
    case R::Background: return t.background;
    case R::Panel: return t.panel;
    case R::Text: return t.foreground;
    case R::Secondary: return t.muted;
    case R::Grid: return t.grid;
    case R::Channel1: return t.channel1;
    case R::Channel2: return t.channel2;
    case R::Math: return t.math;
    case R::Reference: return t.reference;
    case R::Meter: return CurrentMeter;
    case R::Cursor: return t.cursor;
    case R::Selection: return t.accent;
    default: return 0;
    }
}
} // namespace
uint32_t ScopeLook::trigger(uint32_t source) const { const auto c = color(R::Trigger); return c == FollowSource ? source : c; }
uint32_t ScopeLook::tagText(uint32_t fill) const { const auto c = color(R::TagText); return c == Contrast ? ColorContrast::ink(fill) : c; }
bool ScopeLook::operator==(const ScopeLook &o) const {
    return scheme == o.scheme && overrides == o.overrides && !std::memcmp(colors, o.colors, sizeof colors);
}
uint32_t ScopeStyles::base(ScopeScheme scheme, ScopeRole role, const Theme &theme) {
    if (role == R::Trigger) return ScopeLook::FollowSource;
    if (role == R::TagText) return ScopeLook::Contrast;
    if (role == R::Readout) role = R::Text;
    if (scheme == ScopeScheme::Current || unsigned(scheme) >= unsigned(ScopeScheme::Count)) return themeColor(theme, role);
    return schemes[unsigned(scheme) - 1][unsigned(role)];
}
ScopeLook ScopeStyles::look(const Theme &theme, const ScopeAppearance &appearance) {
    ScopeLook look;
    look.scheme = unsigned(appearance.scheme) < unsigned(ScopeScheme::Count) ? appearance.scheme : ScopeScheme::Current;
    for (unsigned i = 0; i < ScopeAppearance::Roles; ++i) {
        const auto role = R(i);
        const auto &value = appearance[role];
        if (!value.inherit() && value.valid()) look.overrides |= uint16_t(1u << i);
        // Readout inherits the resolved Text (an overridden Text included); Text comes first.
        const uint32_t inherited = role == R::Readout ? look.color(R::Text) : base(look.scheme, role, theme);
        look.colors[i] = value.valid() ? value.resolve(inherited) : inherited;
    }
    return look;
}
ScopeRole ScopeStyles::elementRole(ElementId element) {
    switch (element) {
    case ElementId::Parameters: case ElementId::Unit: case ElementId::Preview: return R::Selection;
    case ElementId::Measurements: return R::Readout;
    default: return R::Text;
    }
}
void ScopeStyles::effective(const Theme &theme, const ScopeLook &look, Theme &output) {
    output = theme;
    output.background = look.color(R::Background); output.panel = look.color(R::Panel);
    output.foreground = look.color(R::Text); output.muted = look.color(R::Secondary);
    output.grid = look.color(R::Grid);
    output.channel1 = look.color(R::Channel1); output.channel2 = look.color(R::Channel2);
    output.math = look.color(R::Math); output.reference = look.color(R::Reference);
    output.cursor = look.color(R::Cursor); output.accent = look.color(R::Selection);
    const bool scheme = look.scheme != ScopeScheme::Current;
    auto *elements = output.elements[unsigned(MeasurementMode::Oscilloscope)];
    for (unsigned e = 0; e < ElementCount; ++e) {
        const auto role = elementRole(ElementId(e));
        // Readout left Inherit follows Text, an overridden Text included.
        const bool set = look.overridden(role) || (role == R::Readout && look.overridden(R::Text));
        if (scheme || set) elements[e].color = look.color(role);
    }
}
} // namespace openosc::ui
