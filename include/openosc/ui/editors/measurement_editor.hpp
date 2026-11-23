#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/acquisition/readouts.hpp"
#include "openosc/ui/scope/channel_view.hpp"
#include <cstdio>

namespace openosc::ui {
class MeasurementEditor final : public PropertyList {
public:
    void begin(const scope::ReadoutSelection &selection, plot::ChannelView view = {}) { values_ = selection; view_ = view; }
    const scope::ReadoutSelection &selection() const { return values_; }
    const char *title() const override { return "MEASUREMENTS"; }
    // The eight channel readouts, the DMM trace's four (its MeterKinds only), then the basis.
    static constexpr unsigned PerChannel=scope::ReadoutSelection::PerChannel, MeterRow=2*PerChannel, BasisRow=MeterRow+PerChannel;
    static constexpr unsigned MeterKinds=sizeof scope::ReadoutSelection::MeterKinds/sizeof *scope::ReadoutSelection::MeterKinds;
    unsigned options(unsigned row) const override {
        return row<MeterRow ? scope::Measurements::Count : row<BasisRow ? MeterKinds : row==BasisRow ? 2 : 0;
    }
    unsigned option(unsigned row) const override {
        if (row<MeterRow) return values_.values[view_.input(row / PerChannel)][row % PerChannel];
        if (row==BasisRow) return unsigned(values_.basis);
        for (unsigned i=0; row<BasisRow && i<MeterKinds; ++i)
            if (values_.meter[row-MeterRow]==unsigned(scope::ReadoutSelection::MeterKinds[i])) return i;
        return 0;
    }
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override {
        if (row==BasisRow) std::snprintf(output,capacity,"%s",index ? "Full record (every point)" : "Screen (visible window, fast)");
        else if (row>=MeterRow) {
            const auto kind=index<MeterKinds ? scope::ReadoutSelection::MeterKinds[index] : scope::Measurement::Off;
            std::snprintf(output,capacity,"%s",kind==scope::Measurement::Rms ? "RMS (not in AC functions)" : scope::Measurements::Names[unsigned(kind)]);
        } else std::snprintf(output,capacity,"%s",index<scope::Measurements::Count ? scope::Measurements::Names[index] : "");
    }
    bool choose(unsigned row, unsigned index) override {
        if (index>=options(row)) return false;
        if (row==BasisRow) values_.basis=scope::ReadoutBasis(index);
        else if (row>=MeterRow) values_.meter[row-MeterRow]=uint8_t(scope::ReadoutSelection::MeterKinds[index]);
        else values_.values[view_.input(row / PerChannel)][row % PerChannel]=index;
        return true;
    }
    unsigned count() const override { return BasisRow+1; }
    PropertyKind kind(unsigned) const override { return PropertyKind::Choice; }
    void name(unsigned row, char *output, size_t capacity) const override {
        if (row==BasisRow) std::snprintf(output,capacity,"Measure over");
        else if (row>=MeterRow) std::snprintf(output,capacity,"DMM readout %u",row-MeterRow+1);
        else std::snprintf(output,capacity,"CH%u readout %u",row / PerChannel + 1, row % PerChannel + 1);
    }
private:
    scope::ReadoutSelection values_;
    plot::ChannelView view_;
};
} // namespace openosc::ui
