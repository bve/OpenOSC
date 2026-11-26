#include "openosc/ui/components/menu_palette.hpp"
#include "openosc/ui/components/parameter_bar.hpp"

namespace openosc::ui {
namespace {
// The switch and slider colours menus have always used, whatever the theme.
constexpr uint32_t ThemeTrack = 0x475569, ThemeKnob = 0xe2e8f0;
// High contrast: black and white, a light grey for secondary texts, a dark neutral selection fill
// that white text keeps well above 10:1 on, a mid grey frame (the menu box, inert keys) that shows
// on black and stays quieter than the focus, and a grey track that still shows on black.
constexpr uint32_t Black = 0x000000, White = 0xffffff, Secondary = 0xc8c8c8, Selection = 0x333333, Frame = 0x8a8a8a, Track = 0x6b6b6b;
} // namespace
MenuPalette MenuPalette::of(const Theme &theme, uint32_t accent, MenuContrast contrast) {
    MenuPalette p;
    if (contrast != MenuContrast::High) {
        p.surface = theme.panel; p.frame = theme.grid; p.text = theme.foreground; p.secondary = theme.muted;
        p.selection = theme.grid; p.accent = accent; p.track = ThemeTrack; p.knob = p.knobOn = ThemeKnob;
        return p;
    }
    p.surface = Black; p.frame = Frame; p.text = White; p.secondary = Secondary; p.selection = Selection; p.track = Track;
    // The selected row's value is drawn in the accent on the selection fill: a dim accent (a dark
    // blue of a light scheme, say) gives way to white there and everywhere else it marks the focus.
    p.accent = ParameterBar::contrast(accent, Selection) >= FocusContrast ? accent : White;
    p.knob = White;
    p.knobOn = ParameterBar::ink(p.accent); // A knob over the accent: whichever ink shows on it.
    return p;
}
} // namespace openosc::ui
