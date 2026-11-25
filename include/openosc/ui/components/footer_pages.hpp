#pragma once
#include "openosc/acquisition/readouts.hpp"
#include "openosc/ui/components/scope_footer_layout.hpp"
#include "openosc/ui/scope/channel_view.hpp"
#include <cstdint>

namespace openosc::ui {
// The footer's pages (no LVGL), in fixed cells: with both channels on, each channel's readouts 1-2 in
// its panel, REL then 3-4 (a second page only when one is chosen); with one channel on, its four
// readouts in the four cells of one page. An Off readout leaves its cell empty, the others keep
// theirs; every readout Off, no readout page. A Scales page while scales show, FFT's page first. REL
// turns the pages.
class FooterPages {
public:
    enum class Kind : uint8_t { Spectrum, Readouts, Scales };
    static constexpr uint8_t Empty = 0xff, ChannelOff = 0xfe; // Cell contents besides a slot index.
    struct Cell { uint8_t channel = 0, slot = Empty; };
    static constexpr unsigned Cells = ScopeFooterLayout::Cells, PerChannel = ScopeFooterLayout::PerChannel;
    static constexpr unsigned MaximumPages = ScopeFooterLayout::MaximumPages;

    // True when the plan changed (the page on show is then kept only if it still exists).
    bool plan(const scope::ReadoutSelection &selection, const bool (&enabled)[2], bool spectrum, bool scales);
    unsigned count() const { return count_; }
    unsigned page() const { return page_; }
    void next() { page_ = count_ ? (page_ + 1) % count_ : 0; }
    // Only valid while count() is not zero.
    Kind kind() const { return kind(page_); }
    Kind kind(unsigned page) const;
    // Swap CH1/CH2: with both channels on, the left panel's cells hold the input on screen as CH1.
    void view(const plot::ChannelView &view) { view_ = view; }
    // A readout page's cell: the channel's slot, ChannelOff or Empty.
    Cell cell(unsigned index) const { return cell(page_, index); }
    Cell cell(unsigned page, unsigned index) const;

private:
    bool chosen_[2][scope::ReadoutSelection::PerChannel]{}; // The selection's non-Off readouts.
    bool enabled_[2]{}, spectrum_ = false, scales_ = false;
    plot::ChannelView view_;
    bool wide() const { return enabled_[0] != enabled_[1]; }
    unsigned readoutPages_ = 0, count_ = 0, page_ = 0;
};
} // namespace openosc::ui
