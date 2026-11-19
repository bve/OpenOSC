#pragma once
#include "openosc/acquisition/readouts.hpp"
#include "openosc/ui/components/scope_footer_layout.hpp"
#include <cstdint>

namespace openosc::ui {
// The footer's pages (no LVGL): each channel's readouts two per page in its panel, four with one
// channel on; a Scales page while scales show; FFT's page first. REL turns the pages.
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
    // A readout page's cell: the channel's slot, ChannelOff or Empty.
    Cell cell(unsigned index) const { return cell(page_, index); }
    Cell cell(unsigned page, unsigned index) const;

private:
    uint8_t slots_[2][scope::ReadoutSelection::PerChannel]{}, active_[2]{};
    bool enabled_[2]{}, spectrum_ = false, scales_ = false;
    bool wide() const { return enabled_[0] != enabled_[1]; }
    unsigned readoutPages_ = 0, count_ = 0, page_ = 0;
};
} // namespace openosc::ui
