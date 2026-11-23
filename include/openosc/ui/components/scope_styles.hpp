#pragma once
#include <cstdint>
#include "openosc/settings/scope_appearance.hpp"
#include "openosc/settings/theme.hpp"

namespace openosc::ui {
// Every Scope colour resolved (scheme, then the role overrides), with what it came from: the
// scheme and which roles the user set. Computed when the theme or the appearance changes, never
// per frame.
struct ScopeLook {
    static constexpr uint32_t FollowSource = 0xffffffffu; // trigger: the source channel's colour.
    static constexpr uint32_t Contrast = 0xfffffffeu;     // tagText: ColorContrast::ink of the fill.
    static_assert(ScopeAppearance::Roles <= 16);
    ScopeScheme scheme = ScopeScheme::Current;
    uint16_t overrides = 0; // Bit per ScopeRole the user set (not Inherit).
    uint32_t colors[ScopeAppearance::Roles]{};
    uint32_t color(ScopeRole role) const { return colors[unsigned(role)]; }
    bool overridden(ScopeRole role) const { return overrides >> unsigned(role) & 1u; }
    uint32_t trigger(uint32_t source) const;   // Resolves FollowSource.
    uint32_t tagText(uint32_t fill) const;     // Resolves Contrast.
    uint32_t channel(unsigned c) const { return color(c ? ScopeRole::Channel2 : ScopeRole::Channel1); }
    bool operator==(const ScopeLook &o) const;
    bool operator!=(const ScopeLook &o) const { return !(*this == o); }
};
class ScopeStyles {
public:
    // A scheme's own colour of a role (Current: the theme's field, a constant for DMM; Trigger:
    // FollowSource; Readout: the scheme's Text; TagText: Contrast). What "Default" means in the menu.
    static uint32_t base(ScopeScheme scheme, ScopeRole role, const Theme &theme);
    static ScopeLook look(const Theme &theme, const ScopeAppearance &appearance);
    // The theme the Scope screen and its menus draw with, written into `output` (the UI owner's
    // stable copy): `theme` with the look's background, panel, foreground, muted, grid, channels,
    // math, reference, cursor and accent. The Scope elements' own colours (Theme JSON) are
    // replaced only by what the look carries explicitly: every one under a scheme other than
    // Current; under Current only those whose role the user set (Parameters, Unit, Preview:
    // Selection; Measurements: Readout, or Text while Readout inherits it; the others: Text), by
    // the look's override mask, never by comparing colours. Current with no override is `theme`
    // exactly. Layout, plot options and the DMM and GEN elements stay as they are.
    static void effective(const Theme &theme, const ScopeLook &look, Theme &output);
    static ScopeRole elementRole(ElementId element); // The role an element's colour follows.
};
} // namespace openosc::ui
