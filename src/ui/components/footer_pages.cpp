#include "openosc/ui/components/footer_pages.hpp"
#include <algorithm>

namespace openosc::ui {
namespace {
bool selected(uint32_t value) { return value < scope::Measurements::Count && value != unsigned(scope::Measurement::Off); }
} // namespace
// Fixed cells (plan S06): a readout keeps its place, an Off one leaves its cell empty. Two channels on:
// two cells each, readouts 1-2 then 3-4 (REL); one channel: its four in the four cells, one page.
bool FooterPages::plan(const scope::ReadoutSelection &selection, const bool (&enabled)[2], bool spectrum, bool scales) {
    bool chosen[2][scope::ReadoutSelection::PerChannel]{};
    bool any = false, later = false; // A readout on an enabled channel; one of its 3-4: the second page.
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned i = 0; i < scope::ReadoutSelection::PerChannel; ++i) {
            chosen[c][i] = selected(selection.values[c][i]);
            any = any || (enabled[c] && chosen[c][i]);
            later = later || (enabled[c] && chosen[c][i] && i >= PerChannel);
        }
    // Both channels off: one page that says so ("CHn Off"); every readout Off: none (scales only).
    const unsigned pages = !enabled[0] && !enabled[1] ? 1 : !any ? 0 : enabled[0] != enabled[1] || !later ? 1 : 2;
    const unsigned count = pages + (scales ? 1 : 0) + (spectrum ? 1 : 0);
    bool changed = count != count_ || spectrum != spectrum_ || scales != scales_;
    for (unsigned c = 0; c < 2; ++c) {
        changed = changed || enabled[c] != enabled_[c] || !std::equal(chosen[c], chosen[c] + scope::ReadoutSelection::PerChannel, chosen_[c]);
        std::copy(chosen[c], chosen[c] + scope::ReadoutSelection::PerChannel, chosen_[c]);
        enabled_[c] = enabled[c];
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
    if (wide()) { // The one channel on: readout i in cell i.
        cell.channel = enabled_[1];
        if (chosen_[cell.channel][index]) cell.slot = uint8_t(index);
        return cell;
    }
    cell.channel = uint8_t(index / PerChannel);
    if (!enabled_[cell.channel]) { cell.slot = index % PerChannel ? Empty : ChannelOff; return cell; }
    const unsigned slot = (page - (spectrum_ ? 1 : 0)) * PerChannel + index % PerChannel;
    if (slot < scope::ReadoutSelection::PerChannel && chosen_[cell.channel][slot]) cell.slot = uint8_t(slot);
    return cell;
}
} // namespace openosc::ui
