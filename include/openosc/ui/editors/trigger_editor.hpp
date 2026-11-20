#pragma once
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/trigger_level.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
class TriggerEditor final : public PropertyList {
public:
    enum Row : unsigned { Source, Slope, Level, Mode, Pretrigger, Reject, Midpoint, Fields };
    void begin(const scope::TriggerSettings &settings) { settings_ = settings; }
    void sources(bool ch1, bool ch2) {
        enabled_[0] = ch1; enabled_[1] = ch2;
        if (!enabled_[unsigned(settings_.channel)] && enabled_[unsigned(settings_.channel)^1])
            settings_.channel = scope::Channel(unsigned(settings_.channel)^1);
    }
    void calibration(const scope::FrameMetadata &metadata) { for (unsigned c=0; c<2; ++c) calibration_[c] = metadata.channels[c]; }
    const scope::TriggerSettings &settings() const { return settings_; }
    const char *title() const override { return "TRIGGER"; }
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override { return row == Source || row == Slope || row == Reject ? 2 : row == Mode ? 3 : 0; }
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    void adjust(unsigned row, int direction) override;
    int32_t number(unsigned row) const override { return row == Level ? settings_.level : row == Pretrigger ? settings_.position : 0; }
    void setNumber(unsigned row, int32_t value) override {
        if (row == Level) settings_.level = int16_t(value);
        if (row == Pretrigger) settings_.position = uint16_t(value);
    }
    // A fractional code of this record. Convert to the requested range before quantizing.
    static bool midpoint(const scope::FrameView &frame, scope::Channel channel, double &code);
    void levelEdit(scope::TriggerLevel::Bounds bounds, int step) { levelBounds_ = bounds; levelStep_ = step; }
    int coarseStep(unsigned row) const override { return row == Level ? levelStep_ : 1; }
    unsigned count() const override { return Fields; }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
private:
    bool enabled_[2] = {true, true};
    uint8_t levelStep_ = 1;
    scope::TriggerLevel::Bounds levelBounds_;
    scope::TriggerSettings settings_;
    scope::ChannelMetadata calibration_[2];
};
} // namespace openosc::ui
