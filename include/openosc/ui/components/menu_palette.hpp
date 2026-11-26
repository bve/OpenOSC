#pragma once
#include "openosc/settings/menu_contrast.hpp"
#include "openosc/settings/theme.hpp"

namespace openosc::ui {
// The colours every menu surface draws with: the menu box, its rows, open choice lists, the
// frequency editor, the image list and preview header, and (High only) the keys' cards under a
// menu. One value, resolved when a menu frame is drawn (ApplicationView::menuFrame), so every menu
// type looks the same. Graphics shown in a menu (colour samples, the generator preview) keep their
// own colours: only the frames around them follow.
struct MenuPalette {
    uint32_t surface = 0;   // The menu box, an unselected row, an open list, an inert key's fill.
    uint32_t frame = 0;     // The menu box's border; an inert key's frame.
    uint32_t text = 0;      // Names, values, plain labels, the frame of colour samples.
    uint32_t secondary = 0; // Path, notices, uptime, list counters, inert keys' texts.
    uint32_t selection = 0; // The selected row's fill; a live key's fill.
    uint32_t accent = 0;    // Focus: the title, the selected row's border and value, a list's border, "More".
    uint32_t track = 0;     // A switch's and a slider's empty track.
    uint32_t knob = 0, knobOn = 0; // A switch's knob, off and on (over the accent).
    // Theme: the theme's own menu colours with the menu's accent, exactly as before GitHub #44.
    // High: black surfaces, white text, light grey secondary text, a dark neutral selection fill
    // and the accent kept only where it stands out on that fill (else white) for the focus.
    static MenuPalette of(const Theme &theme, uint32_t accent, MenuContrast contrast);
    static constexpr float FocusContrast = 4.5f; // WCAG AA for text: the accent on the selection fill.
    bool operator==(const MenuPalette &o) const {
        return surface == o.surface && frame == o.frame && text == o.text && secondary == o.secondary && selection == o.selection &&
               accent == o.accent && track == o.track && knob == o.knob && knobOn == o.knobOn;
    }
    bool operator!=(const MenuPalette &o) const { return !(*this == o); }
};
} // namespace openosc::ui
