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
    static constexpr unsigned PerChannel = scope::ReadoutSelection::PerChannel;
    static constexpr unsigned BasisRow = 2 * PerChannel;
    unsigned options(unsigned row) const override { return row<BasisRow ? scope::Measurements::Count : row==BasisRow ? 2 : 0; }
    unsigned option(unsigned row) const override {
        return row<BasisRow ? values_.values[view_.input(row / PerChannel)][row % PerChannel] : row==BasisRow ? unsigned(values_.basis) : 0;
    }
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override {
        if (row==BasisRow) std::snprintf(output,capacity,"%s",index ? "Full record (every point)" : "Screen (visible window, fast)");
        else std::snprintf(output,capacity,"%s",index<scope::Measurements::Count ? scope::Measurements::Names[index] : "");
    }
    bool choose(unsigned row, unsigned index) override {
        if (index>=options(row)) return false;
        if (row==BasisRow) values_.basis=scope::ReadoutBasis(index);
        else values_.values[view_.input(row / PerChannel)][row % PerChannel]=index;
        return true;
    }
    unsigned count() const override { return BasisRow+1; }
    PropertyKind kind(unsigned) const override { return PropertyKind::Choice; }
    void name(unsigned row, char *output, size_t capacity) const override {
        if (row==BasisRow) std::snprintf(output,capacity,"Measure over");
        else std::snprintf(output,capacity,"CH%u readout %u",row / PerChannel + 1, row % PerChannel + 1);
    }
private:
    scope::ReadoutSelection values_;
    plot::ChannelView view_;
};
} // namespace openosc::ui
