#pragma once
#include <cstdint>
#include "openosc/settings/color_value.hpp"

namespace openosc {
// The Scope's look only: a scheme and per-role colours over it. It never reaches the capture, the
// trigger, HOLD or the measurements; changing it repaints, nothing else.
enum class ScopeScheme : uint8_t {
    Current,      // The Theme JSON's colours, as today.
    HighContrast, // Black body, pure channel colours, white text.
    Daylight,     // Light body, dark text and tags, deep channel colours.
    Amber,        // Dark amber, for the night.
    Count
};
enum class ScopeRole : uint8_t {
    Background,  // The plot and the Scope body.
    Panel,       // Menus, popups, the footer panels' base.
    Text,        // Primary text (Theme::foreground).
    Secondary,   // Muted text, disabled channels (Theme::muted).
    Grid,        // Graticule, separators.
    Channel1, Channel2, Math, Reference,
    Meter,       // The Scope's DMM trace: its line, zero tag, cards and footer panel (no Theme field).
    Cursor,      // Cursor lines and their tags.
    Trigger,     // Trigger level/position marks; Inherit: the source channel's colour (as today).
    Selection,   // Softkey/menu selection and focus (Theme::accent).
    Readout,     // Neutral readout text (rate, time/div, Screen readouts); Inherit: Text.
    TagText,     // Text on coloured tags and lit panels; Inherit: contrast of the tag's fill.
    Count
};
struct ScopeAppearance {
    static constexpr unsigned Roles = unsigned(ScopeRole::Count);
    ScopeScheme scheme = ScopeScheme::Current;
    ColorValue roles[Roles]{};
    const ColorValue &operator[](ScopeRole role) const { return roles[unsigned(role)]; }
    ColorValue &operator[](ScopeRole role) { return roles[unsigned(role)]; }
    bool custom() const; // Any role override.
    bool valid() const;
    bool operator==(const ScopeAppearance &o) const;
    bool operator!=(const ScopeAppearance &o) const { return !(*this == o); }
};
struct ScopeRoleInfo { const char *name, *key; };
class ScopeAppearanceCatalog {
public:
    static const ScopeRoleInfo &role(ScopeRole role);
    static const char *schemeName(ScopeScheme scheme); // "Current", "High contrast", "Daylight", "Amber"
    static const char *schemeKey(ScopeScheme scheme);  // "current", "high_contrast", "daylight", "amber"
    static ScopeScheme schemeByKey(const char *key);   // Current if unknown.
    static ScopeRole roleByKey(const char *key);       // ScopeRole::Count if unknown.
};
} // namespace openosc
