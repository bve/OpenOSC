#pragma once
#include "openosc/ui/components/status_bar_layout.hpp"

namespace openosc::ui {
// The Scope footer: one row as high as the status bar, at the bottom of the panel, with the
// status bar's margins. Two panels, each a thin frame round two readouts in fixed cells: CH1 left
// and CH2 right, or both for the one channel on; the page dots stand in the gap between them. Texts are IBM Plex Sans 12 (14-pixel
// line), centred on the row like the status pill's. The panel's bottom frame row is the Full
// basis rail (ScopeFooter: lit as the pass covers the record).
struct ScopeFooterLayout {
    using Column = StatusColumn;
    using Bar = StatusBarLayout;
    static constexpr int Height = Bar::Height, Y = 240 - Height;
    static constexpr int Line = 14, TextY = (Height - Line) / 2;
    static constexpr int Panels = 2, PerChannel = 2, Cells = Panels * PerChannel; // PerChannel: readouts per panel.
    static constexpr int PanelGap = 4, PanelY = 1, PanelHeight = Height - 2 * PanelY, Border = 1;
    static constexpr int PanelWidth = (Bar::Width - 2 * Bar::Margin - PanelGap) / Panels; // 156
    static constexpr Column panel(int channel) { return {Bar::Margin + channel * (PanelWidth + PanelGap), PanelWidth}; }
    // The text inside a panel: a frame pixel and one of padding on each side.
    static constexpr int Inset = Border + 1;
    static constexpr int TextWidth = PanelWidth - 2 * Inset; // 152
    static constexpr Column text(int channel) { return {panel(channel).x + Inset, TextWidth}; }
    // Two cells of the panel's text (76 px); a text keeps CellGap to the next one.
    static constexpr int CellWidth = TextWidth / PerChannel, CellGap = Bar::Tight;
    static constexpr int RailY = PanelY + PanelHeight - Border; // The bottom frame row.
    static constexpr Column rail(int channel) { return {panel(channel).x + Border, PanelWidth - 2 * Border}; }
    static constexpr int PageDot = 2, PageDotStep = 4, MaximumPages = 4;
    static constexpr Column Pager{Bar::Margin + PanelWidth + (PanelGap - PageDot) / 2, PageDot}; // Centred in the gap.
    // The first row past the waveform: one free row above the footer, as under the parameter cards.
    static constexpr int PlotEnd = Y - 1;
};
namespace scope_footer_checks {
using F = ScopeFooterLayout;
static_assert(F::Height == StatusBarLayout::Height, "The footer is exactly as high as the status bar");
static_assert(F::Y + F::Height == 240, "The footer ends at the panel's bottom");
static_assert(F::panel(1).right() + StatusBarLayout::Margin == StatusBarLayout::Width, "The panels keep the status bar's margins");
static_assert(F::text(0).width - F::PerChannel * F::CellWidth < F::PerChannel, "The cells share a panel's text");
static_assert(F::Pager.x >= F::panel(0).right() && F::Pager.right() <= F::panel(1).x, "The page dots touch neither panel");
static_assert(F::TextY > F::PanelY && F::TextY + F::Line < F::RailY, "Text rows inside the frame, clear of the rail");
static_assert(F::MaximumPages * F::PageDotStep <= F::Height, "Every page has a dot in the row");
} // namespace scope_footer_checks
} // namespace openosc::ui
