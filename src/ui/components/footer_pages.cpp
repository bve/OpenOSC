#include "openosc/ui/components/footer_pages.hpp"
#include <algorithm>

namespace openosc::ui {
namespace {
bool selected(uint32_t value) { return value < scope::Measurements::Count && value != unsigned(scope::Measurement::Off); }
} // namespace
// Fixed cells (plan S06): a readout keeps its place, an Off one leaves its cell empty. Two channels on:
// two cells each, readouts 1-2 then 3-4 (REL); one channel: its four in the four cells, one page. The
// DMM trace in the panel of a channel that is off counts as that channel; with both on, its own page.
bool FooterPages::plan(const scope::ReadoutSelection &selection, const bool (&enabled)[2], bool spectrum, bool scales, bool meter) {
    constexpr unsigned Slots = scope::ReadoutSelection::PerChannel;
    bool chosen[Sources][Slots]{};
    for (unsigned i = 0; i < Slots; ++i) {
        for (unsigned c = 0; c < 2; ++c) chosen[c][i] = selected(selection.values[c][i]);
        chosen[Meter][i] = selected(selection.meter[i]);
    }
    // What the readout pages show: the channels on, and the DMM trace in a panel of a channel that is off.
    const bool shown[Sources] = {enabled[0], enabled[1], meter && !(enabled[0] && enabled[1])};
    bool any = false, later = false; // A readout shown there; one of its 3-4: the second page.
    for (unsigned s = 0; s < Sources; ++s)
        for (unsigned i = 0; i < Slots; ++i) {
            any = any || (shown[s] && chosen[s][i]);
            later = later || (shown[s] && chosen[s][i] && i >= PerChannel);
        }
    const bool wide = meter ? !enabled[0] && !enabled[1] : enabled[0] != enabled[1];
    // Both channels off without the DMM trace: one page that says so ("CHn Off"); every readout Off: none (scales only).
    const unsigned pages = !enabled[0] && !enabled[1] && !meter ? 1 : !any ? 0 : wide || !later ? 1 : 2;
    const unsigned meterPages = meter && enabled[0] && enabled[1] && std::find(chosen[Meter], chosen[Meter] + Slots, true) != chosen[Meter] + Slots;
    const unsigned count = pages + meterPages + (scales ? 1 : 0) + (spectrum ? 1 : 0);
    bool changed = count != count_ || spectrum != spectrum_ || scales != scales_ || meter != meter_ || meterPages != meterPages_;
    for (unsigned s = 0; s < Sources; ++s) {
        changed = changed || !std::equal(chosen[s], chosen[s] + Slots, chosen_[s]);
        std::copy(chosen[s], chosen[s] + Slots, chosen_[s]);
    }
    for (unsigned c = 0; c < 2; ++c) {
        changed = changed || enabled[c] != enabled_[c];
        enabled_[c] = enabled[c];
    }
    spectrum_ = spectrum; scales_ = scales; meter_ = meter; readoutPages_ = pages; meterPages_ = meterPages; count_ = count;
    if (page_ >= count_) page_ = 0;
    return changed;
}
FooterPages::Kind FooterPages::kind(unsigned page) const {
    if (spectrum_ && !page) return Kind::Spectrum;
    return page - (spectrum_ ? 1 : 0) < readoutPages_ + meterPages_ ? Kind::Readouts : Kind::Scales;
}
FooterPages::Cell FooterPages::cell(unsigned page, unsigned index) const {
    Cell cell;
    if (index >= Cells || kind(page) != Kind::Readouts) return cell;
    const unsigned readouts = page - (spectrum_ ? 1 : 0); // The readout page's index.
    if (wide() || readouts == readoutPages_) { // One source (the DMM trace's own page too): readout i in cell i.
        cell.channel = uint8_t(readouts == readoutPages_ || meter_ ? Meter : enabled_[1]);
        if (chosen_[cell.channel][index]) cell.slot = uint8_t(index);
        return cell;
    }
    const unsigned panel = index / PerChannel, input = view_.input(panel); // The input on screen in that panel.
    cell.channel = uint8_t(enabled_[input] || !meter_ ? input : Meter);
    if (cell.channel != Meter && !enabled_[cell.channel]) { cell.slot = index % PerChannel ? Empty : ChannelOff; return cell; }
    const unsigned slot = readouts * PerChannel + index % PerChannel;
    if (slot < scope::ReadoutSelection::PerChannel && chosen_[cell.channel][slot]) cell.slot = uint8_t(slot);
    return cell;
}
} // namespace openosc::ui
