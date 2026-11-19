#include "openosc/ui/components/footer_pages.hpp"
#include <algorithm>

namespace openosc::ui {
bool FooterPages::plan(const scope::ReadoutSelection &selection, const bool (&enabled)[2], bool spectrum, bool scales) {
    uint8_t slots[2][scope::ReadoutSelection::PerChannel]{}, active[2]{};
    unsigned pages = 0;
    for (unsigned c = 0; c < 2; ++c) {
        for (unsigned i = 0; i < scope::ReadoutSelection::PerChannel; ++i)
            if (selection.values[c][i] < scope::Measurements::Count && selection.values[c][i] != unsigned(scope::Measurement::Off))
                slots[c][active[c]++] = uint8_t(i);
    }
    for (unsigned c = 0; c < 2; ++c) {
        const unsigned per = enabled[0] != enabled[1] ? Cells : PerChannel;
        if (enabled[c]) pages = std::max(pages, (active[c] + per - 1) / per);
    }
    const unsigned count = pages + (scales ? 1 : 0) + (spectrum ? 1 : 0);
    bool changed = count != count_ || spectrum != spectrum_ || scales != scales_;
    for (unsigned c = 0; c < 2; ++c) {
        changed = changed || enabled[c] != enabled_[c] || active[c] != active_[c] ||
                  !std::equal(slots[c], slots[c] + active[c], slots_[c]);
        std::copy(slots[c], slots[c] + scope::ReadoutSelection::PerChannel, slots_[c]);
        active_[c] = active[c]; enabled_[c] = enabled[c];
    }
    spectrum_ = spectrum; scales_ = scales; readoutPages_ = pages; count_ = count;
    if (page_ >= count_) page_ = 0;
    return changed;
}
FooterPages::Kind FooterPages::kind(unsigned page) const {
    if (spectrum_ && !page) return Kind::Spectrum;
    return page - (spectrum_ ? 1 : 0) < readoutPages_ ? Kind::Readouts : Kind::Scales;
}
FooterPages::Cell FooterPages::cell(unsigned page, unsigned index) const {
    Cell cell;
    if (index >= Cells || kind(page) != Kind::Readouts) return cell;
    const unsigned readouts = page - (spectrum_ ? 1 : 0);
    if (wide()) {
        cell.channel = enabled_[1];
        const unsigned item = readouts * Cells + index;
        if (item < active_[cell.channel]) cell.slot = slots_[cell.channel][item];
        return cell;
    }
    cell.channel = uint8_t(index / PerChannel);
    if (!enabled_[cell.channel]) { cell.slot = index % PerChannel ? Empty : ChannelOff; return cell; }
    const unsigned item = readouts * PerChannel + index % PerChannel;
    if (item < active_[cell.channel]) cell.slot = slots_[cell.channel][item];
    return cell;
}
} // namespace openosc::ui
