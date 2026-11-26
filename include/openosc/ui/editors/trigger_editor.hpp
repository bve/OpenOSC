#pragma once
#include "openosc/acquisition/analysis.hpp"
#include "openosc/acquisition/trigger_level.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/ui/scope/channel_view.hpp"

namespace openosc::ui {
// The Trigger menu. Its rows depend on the trigger type (GitHub #133): an edge trigger shows Type and
// the edge rows, each at its Field's position; a qualified one adds its levels, condition and time
// limits after the polarity. Callers name rows by field (field(row), row(field)).
class TriggerEditor final : public PropertyList {
public:
    enum Field : unsigned {
        Type, Source, Slope, Level, Mode, Pretrigger, Center, Reject, Midpoint,
        Level2, Condition, Lower, Upper, Fields
    };
    void begin(const scope::TriggerSettings &settings) { settings_ = settings; }
    void channelView(plot::ChannelView view) { view_ = view; }
    void sources(bool ch1, bool ch2) {
        enabled_[0] = ch1; enabled_[1] = ch2;
        if (!enabled_[unsigned(settings_.channel)] && enabled_[unsigned(settings_.channel)^1])
            settings_.channel = scope::Channel(unsigned(settings_.channel)^1);
    }
    void calibration(const scope::FrameMetadata &metadata) { for (unsigned c=0; c<2; ++c) calibration_[c] = metadata.channels[c]; }
    const scope::TriggerSettings &settings() const { return settings_; }
    const char *title() const override { return "TRIGGER"; }
    // The field shown at a row, and the row a field is at (count() when it is not shown).
    Field field(unsigned row) const;
    unsigned row(Field field) const;
    // A level row (Level, Level2): its steps are the level keys'.
    bool levelRow(unsigned row) const { const auto f = field(row); return f == Level || f == Level2; }
    // A level row editing TriggerSettings::level, the one the level keys move and the following anchors.
    bool keysLevel(unsigned row) const { return levelRow(row) && primary(field(row)); }
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    void adjust(unsigned row, int direction) override;
    int32_t number(unsigned row) const override;
    void setNumber(unsigned row, int32_t value) override;
    // A fractional code of this record. Convert to the requested range before quantizing.
    static bool midpoint(const scope::FrameView &frame, scope::Channel channel, double &code);
    void levelEdit(scope::TriggerLevel::Bounds bounds, int step) { levelBounds(bounds); levelStep_ = step; }
    // The Level row's range (the plot's): Level steps stop at its ends and its value is marked "(max)"
    // or "(min)" there (issue #61). True when that mark changes.
    bool levelBounds(scope::TriggerLevel::Bounds bounds) {
        const int before = levelEnd();
        levelBounds_ = bounds;
        return levelEnd() != before;
    }
    int levelEnd() const { return end(settings_.level); }
    int coarseStep(unsigned row) const override {
        const auto f = field(row);
        return f == Level || f == Level2 ? levelStep_ : f == Lower || f == Upper ? TimeSteps / 3 : 1;
    }
    unsigned count() const override;
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    // The time limits' steps: 1, 1.2, 1.5, 2, 2.5, 3, 4, 5, 6, 7, 8, 9 x a power of ten nanoseconds,
    // TriggerSettings::MinimumNs..MaximumNs; a value between two steps moves to the next one.
    static uint32_t stepTime(uint32_t ns, int steps);
    static constexpr int TimeSteps = 12; // Per decade.

private:
    bool primary(Field f) const; // Whether a level row's field is `level`, the one the level keys move.
    int end(int level) const { return level == levelBounds_.high ? 1 : level == levelBounds_.low ? -1 : 0; }
    bool windowed() const {
        return settings_.condition == scope::TriggerCondition::Inside || settings_.condition == scope::TriggerCondition::Outside;
    }
    // The settings' field a level row edits: Level the band's high end, Level2 its low end (one
    // level: `level`). Ties: `level` is the high end.
    int16_t &levelField(Field f);
    int16_t levelValue(Field f) const;
    unsigned rows(Field (&out)[Fields]) const;
    bool enabled_[2] = {true, true};
    uint8_t levelStep_ = 1;
    plot::ChannelView view_;
    scope::TriggerLevel::Bounds levelBounds_;
    scope::TriggerSettings settings_;
    scope::ChannelMetadata calibration_[2];
};
} // namespace openosc::ui
