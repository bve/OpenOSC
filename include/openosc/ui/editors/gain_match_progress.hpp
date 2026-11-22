#pragma once
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/calibration/channel_match.hpp"

namespace openosc::ui {
// Scope calibration > Gain > Match to other channel (GitHub #80), step by step: what is corrected
// (the target channel at the V/div both run at) and the probes; the measurement's progress; the swap
// of the probes; the review: both spans, each probe's compensation (reported, not corrected), the
// probes' difference after a swap, the correction and the factor in force, Save / Swap probes /
// Cancel. Nothing is saved before Save.
class GainMatchProgress final : public PropertyList {
public:
    enum class Row : uint8_t { Target, Range, Probes, Progress, Ratio, Spans, Compensation, Probe, Correction,
                               Resolution, Measure, Measuring, Swapped, Save, Swap, Cancel };
    explicit GainMatchProgress(const calibration::ChannelMatch &match) : match_(match) {}
    const char *title() const override { return "MATCH CHANNEL GAIN"; }
    const char *notice() const override;
    unsigned count() const override;
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    Row at(unsigned row) const;
    unsigned defaultRow() const; // The step's main action (or Cancel while measuring).
private:
    const calibration::ChannelMatch &match_;
};
} // namespace openosc::ui
