#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/acquisition/readouts.hpp"
#include "openosc/ui/scope/channel_view.hpp"
#include <cstdio>

namespace openosc::ui {
class MeasurementEditor final : public PropertyList {
public:
    void begin(const scope::ReadoutSelection &selection, const scope::MeterReadoutSelection &meter, plot::ChannelView view = {}) {
        values_ = selection; meter_ = meter; view_ = view;
    }
    const scope::ReadoutSelection &selection() const { return values_; }
    const scope::MeterReadoutSelection &meter() const { return meter_; }
    const char *title() const override { return "MEASUREMENTS"; }
    // The eight channel readouts, the DMM trace's four (MeterReadoutSelection::Kinds only), the basis, then the short window.
    static constexpr unsigned PerChannel=scope::ReadoutSelection::PerChannel, MeterRow=2*PerChannel, BasisRow=MeterRow+PerChannel;
    static constexpr unsigned MeterKinds=sizeof scope::MeterReadoutSelection::Kinds/sizeof *scope::MeterReadoutSelection::Kinds;
    // Short window f/T (GitHub #171): Off, or f and T a window too short resolves from the counter.
    static constexpr unsigned ShortWindowRow = BasisRow + 1;
    unsigned options(unsigned row) const override {
        return row<MeterRow ? scope::Measurements::Count : row<BasisRow ? MeterKinds : row<=ShortWindowRow ? 2 : 0;
    }
    unsigned option(unsigned row) const override {
        if (row<MeterRow) return values_.values[view_.input(row / PerChannel)][row % PerChannel];
        if (row==BasisRow) return unsigned(values_.basis);
        if (row==ShortWindowRow) return unsigned(values_.shortWindowCounter);
        for (unsigned i=0; row<BasisRow && i<MeterKinds; ++i)
            if (meter_.values[row-MeterRow]==unsigned(scope::MeterReadoutSelection::Kinds[i])) return i;
        return 0;
    }
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override {
        if (row==BasisRow) std::snprintf(output,capacity,"%s",index ? "Full record (every point)" : "Screen (visible window, fast)");
        else if (row==ShortWindowRow) std::snprintf(output,capacity,"%s",index ? "From the counter" : "Off");
        else if (row>=MeterRow) {
            const auto kind=index<MeterKinds ? scope::MeterReadoutSelection::Kinds[index] : scope::Measurement::Off;
            std::snprintf(output,capacity,"%s",kind==scope::Measurement::Rms ? "RMS (not in AC functions)" : scope::Measurements::Names[unsigned(kind)]);
        } else std::snprintf(output,capacity,"%s",index<scope::Measurements::Count ? scope::Measurements::Names[index] : "");
    }
    bool choose(unsigned row, unsigned index) override {
        if (index>=options(row)) return false;
        if (row==BasisRow) values_.basis=scope::ReadoutBasis(index);
        else if (row==ShortWindowRow) values_.shortWindowCounter=index!=0;
        else if (row>=MeterRow) meter_.values[row-MeterRow]=uint8_t(scope::MeterReadoutSelection::Kinds[index]);
        else values_.values[view_.input(row / PerChannel)][row % PerChannel]=index;
        return true;
    }
    unsigned count() const override { return ShortWindowRow+1; }
    PropertyKind kind(unsigned) const override { return PropertyKind::Choice; }
    void name(unsigned row, char *output, size_t capacity) const override {
        if (row==BasisRow) std::snprintf(output,capacity,"Measure over");
        else if (row==ShortWindowRow) std::snprintf(output,capacity,"Short window f/T");
        else if (row>=MeterRow) std::snprintf(output,capacity,"DMM readout %u",row-MeterRow+1);
        else std::snprintf(output,capacity,"CH%u readout %u",row / PerChannel + 1, row % PerChannel + 1);
    }
private:
    scope::ReadoutSelection values_;
    scope::MeterReadoutSelection meter_;
    plot::ChannelView view_;
};
} // namespace openosc::ui
