#include "openosc/settings/theme.hpp"
#include "openosc/ui/components/scope_footer_layout.hpp"
#include "openosc/ui/scope/plot_frame.hpp"

namespace openosc {
Theme Theme::defaults(LayoutPreset preset) {
    Theme theme;
    theme.layout = preset;
    for (unsigned mode = 0; mode < InstrumentModeCount; ++mode) {
        auto* e = theme.elements[mode];
        auto element = [e](ElementId id) -> ElementStyle& { return e[static_cast<unsigned>(id)]; };
        element(ElementId::Status) = {10, 8, 300, 20, theme.foreground, 12, true, {}};
        // The waveform object is its plot (300 columns at x 12) plus the marker margins (PlotFrame).
        using Frame = plot::PlotFrame;
        element(ElementId::Waveform) = {Frame::ObjectX, Frame::objectY(38), Frame::ObjectWidth, 152 + Frame::Top,
                                        theme.foreground, 12, mode == 0, {}};
        element(ElementId::Measurements) = {10, int16_t(mode ? 55 : 198), 300, 18, theme.foreground, 12, true, {}};
        element(ElementId::Reading) = {25, 87, 230, 48, theme.foreground, 32, mode != 0, {}};
        element(ElementId::Unit) = {240, 102, 70, 32, theme.accent, 24, mode != 0, {}};
        element(ElementId::Icon) = {280, 8, 24, 24, theme.foreground, 12, false, {}};
        element(ElementId::Parameters) = {2, 22, 316, 32, theme.accent, 12, false, {}}; // ScreenLayout: four equal 76 px zones.
        if (!mode) element(ElementId::Parameters).color = theme.channel1;
        element(ElementId::Details) = {12, 66, 296, 18, theme.foreground, 14, false, {}};
        element(ElementId::Preview) = {12, 179, 296, 52, theme.accent, 12, false, {}};
        if (preset != LayoutPreset::Legacy) {
            element(ElementId::Status) = {6, 2, 160, 16, theme.foreground, 12, true, {}};
            element(ElementId::Parameters).visible = true;
            // Scope: the plot reaches the one-row footer (ui::ScopeFooterLayout), as high as the status bar.
            using Footer = ui::ScopeFooterLayout;
            // From the row after the parameter cards (y 22..53) down to the footer.
            element(ElementId::Waveform) = {Frame::ObjectX, 55, Frame::ObjectWidth, Footer::PlotEnd - 55, theme.foreground, 12, mode == 0, {}};
            element(ElementId::Measurements) = mode ? ElementStyle{8, 209, 304, 29, theme.foreground, 12, true, {}}
                                                    : ElementStyle{0, Footer::Y, ui::StatusBarLayout::Width, Footer::Height, theme.foreground, 12, true, {}};
            element(ElementId::Reading) = {12, 84, 240, 57, theme.foreground, 40, mode != 0, {}};
            element(ElementId::Unit) = {252, 103, 60, 32, theme.accent, 24, mode != 0, {}};
            element(ElementId::Details).visible = mode != 0;

            if (mode == 2) {
                element(ElementId::Measurements) = {12, 143, 296, 32, theme.foreground, 12, true, {}};
                element(ElementId::Preview).visible = true;
                element(ElementId::Preview).y = 170;
                element(ElementId::Preview).height = 68;
                element(ElementId::Preview).color = 0xfb923c;
                element(ElementId::Unit).color = 0xfb923c;
                element(ElementId::Parameters).color = 0xfb923c;
            }
            if (!mode && preset == LayoutPreset::Compact) {
                element(ElementId::Parameters).visible = false;
                element(ElementId::Details) = {10, 24, 300, 16, theme.foreground, 12, true, {}};
                element(ElementId::Waveform).y = 42; // Below the details line (y 24..39).
                element(ElementId::Waveform).height = Footer::PlotEnd - 42;
            }
        }
        if (mode == 1) { // The DMM body, the same in every preset (Legacy differs in the shell only).
            // The function sign (FunctionIcon, at the Reading's x) and the state on one row with the
            // unit badge at its right; the reading below them across the full width.
            element(ElementId::Details) = {72, 62, 160, 18, theme.foreground, 14, true, {}};
            element(ElementId::Unit) = {236, 56, 72, 26, theme.accent, 24, true, {}};
            element(ElementId::Reading) = {12, 82, 296, 64, theme.foreground, 64, true, {}};
            element(ElementId::Measurements) = {12, 149, 296, 89, theme.foreground, 12, true, {}};
        }
        for (unsigned i = 0; i < ElementCount; ++i) e[i].zOrder = i;
    }
    // Sinc between the points of a zoomed trace (GitHub #34: a 20 MHz sine at 100 MS/s is 5 points a
    // period; linear draws it as a zigzag). For a new or reset theme only: theme.json keeps its choice.
    theme.plot.interpolation = plot::Interpolation::Sinc;
    return theme;
}
}
