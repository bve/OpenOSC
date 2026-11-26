#pragma once
#include "openosc/acquisition/request.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/ui/scope/options.hpp"
#include "openosc/ui/scope/channel_view.hpp"

namespace openosc::ui {
class ScopeEditor : public PropertyList {
public:
    // channel: CH1, CH2 or (ChannelEditor::Meter) the DMM trace.
    void begin(const ScopeRequest &request, const plot::Options &plot, unsigned channel) {
        request_ = request; plot_ = plot; channel_ = channel < 3 ? channel : 0; notice_ = nullptr;
    }
    // Why the last change was refused, for a toast; null when it applied.
    const char *notice() const override { return notice_; }
    const ScopeRequest &request() const { return request_; }
    const plot::Options &plot() const { return plot_; }
    unsigned channel() const { return channel_; }
protected:
    ScopeRequest request_;
    plot::Options plot_;
    unsigned channel_ = 0;
    const char *notice_ = nullptr;
};
class ChannelEditor final : public ScopeEditor {
public:
    // Trace brightness (GitHub #86), Density and its colour (GitHub #85) are the view's, as Invert;
    // the colour row only for Mono.
    enum Row : unsigned { Channel, Enabled, Coupling, Probe, Scale, Zero, Invert, TraceBrightness, Density, DensityColor, Fields };
    void channelView(plot::ChannelView view) { view_ = view; }
    // The DMM trace, the Channel row's third choice: its rows are Channel, Enabled (the trace shown) and
    // Function, the DMM's (Preferences::meterFunction, a MeterFunctions index: the DMM screen's too).
    static constexpr unsigned Meter = 2, Function = Coupling;
    // meterFunction: the DMM's in force (Preferences::meterFunction), at every opening and refresh.
    void begin(const ScopeRequest &request, const plot::Options &plot, unsigned channel, unsigned meterFunction) {
        ScopeEditor::begin(request, plot, channel);
        meterFunction_ = meterFunction;
    }
    bool meter() const { return channel_ == Meter; }
    unsigned meterFunction() const { return meterFunction_; }
    const char *title() const override { return "CHANNEL"; }
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    void adjust(unsigned row, int direction) override;
    int32_t number(unsigned row) const override { return row == Zero && !meter() ? request_.frontend.position[channel_] : 0; }
    void setNumber(unsigned row, int32_t value) override { if (row == Zero && !meter()) request_.frontend.position[channel_] = value; }
    unsigned count() const override {
        return meter() ? Function + 1 : plot_.channels[channel_].grade == plot::Grade::Mono ? Fields : DensityColor;
    }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    bool optionSwatch(unsigned row, unsigned option, uint32_t &rgb) const override;
    bool valueSwatch(unsigned row, uint32_t &rgb) const override { return optionSwatch(row, option(row), rgb); }
private:
    plot::ChannelView view_;
    unsigned meterFunction_ = 0;
};
class AcquisitionEditor final : public ScopeEditor {
public:
    enum Row : unsigned { Timebase, Memory, Captured, Rate, Rolling, DeepMemory, Fields };
    const char *title() const override { return "ACQUISITION"; }
    void snapshot(const scope::FrameMetadata &metadata) { metadata_ = metadata; }
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    unsigned count() const override { return Fields; }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
private:
    scope::FrameMetadata metadata_;
};
} // namespace openosc::ui
