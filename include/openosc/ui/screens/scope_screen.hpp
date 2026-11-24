#pragma once
#include <cstring>
#include "openosc/acquisition/readouts.hpp"
#include "openosc/acquisition/trend.hpp"
#include "openosc/ui/components/readout_text.hpp"
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/generator/waveform.hpp"
#include "openosc/ui/scope/cursor_control.hpp"
#include "openosc/ui/scope/meter_readouts.hpp"
#include "openosc/ui/screens/instrument_screen.hpp"

namespace openosc::ui {
class ScopeScreen final : public InstrumentScreen {
public:
    using InstrumentScreen::InstrumentScreen;
    void activate(const ApplicationState &state) override;
    void update(const scope::FrameView &frame, bool held, unsigned channel, DataStatus status);
    void status(DataStatus status, bool held);
    void settings(const scope::FrameMetadata &next, unsigned channel, bool nextTrend);
    // panReverse: Settings > Display > Pan direction, for Zoom & Pan's Left/Right hint.
    void context(unsigned channel, plot::Focus focus, bool panReverse = false);
    // The DMM trace on the Scale and Position cards while F1/F2 select it: its units/div in `unit`
    // (plot::MeterScale) and the DMM function's name. Not selected: the channel's cards.
    struct Meter {
        bool selected = false;
        double perDivision = 0;
        const char *unit = "", *function = "";
    };
    void meter(const Meter &meter);
    // The DMM trace's readouts and scale to the footer, each display pass (not under a panel).
    void meterReadouts(const plot::MeterReadouts &readouts);
    void generator(const generator::Snapshot &output, bool captureHalved);
    // The trend copy on show (DataAccess::trendShown), valid or not; nowUs: the frames' clock
    // (DataAccess::nowUs), for a running trend's elapsed time in the status pill.
    void trend(const scope::TrendView &copy, uint64_t nowUs);
    // Settings > Display > Swap CH1/CH2: the plot, footer and cards relabelled at once (HOLD included).
    void channelView(const plot::ChannelView &view) { host_.channelView(view); cards(); }
    void counters(const scope::CounterSnapshot &snapshot);
    // False when nothing was painted (no frame shown yet, or a panel over the scope).
    bool readouts(const scope::ReadoutResult &result, const ReadoutState &state);
    // The readout labels still show the last result painted (activate() clears them).
    bool readoutsPainted() const { return readoutsPainted_; }
    void nextFooterPage(); // REL: the next page of the footer row.
    void autoSetup(bool active) { automatic_=active; status(health_,statusHeld_); }
    void calibrating(bool active) { calibrating_=active; status(health_,statusHeld_); }
    void triggerState(scope::TriggerMode mode, bool waiting) {
        const bool label = triggerMode_ != mode;
        triggerMode_ = mode; waiting_ = waiting; status(health_, statusHeld_);
        if (label) cards();
    }
    void triggerType(scope::TriggerType type) {
        if (triggerType_ == type) return;
        triggerType_ = type;
        cards();
    }

private:
    scope::FrameMetadata next_; // The acquisition settings of the next capture.
    scope::ReadoutSelection measurements_;
    scope::CounterSnapshot counters_;
    bool countersValid_=false, readoutsPainted_=false;
    void cards();
    void select(unsigned channel); // The cards' channel, its trace drawn in front (#81).
    unsigned channel_ = 0;
    Meter meter_;
    // trending_: a trend copy is on (generator()); nextTrend_: the next capture is a trend (settings()).
    bool held_ = false, valid_ = false, trending_ = false, nextTrend_ = false;
    plot::Focus focus_ = plot::Focus::Scale;
    bool panReverse_ = false;
    void zoomNote(); // Zoom & Pan's factor and arrows in the plot's note (the GEN note returns on the next pass).
    char status_[24] = ""; // The pill's text as shown: a trend's carries its elapsed time (status()).
    // A trend's elapsed whole seconds (the copy's points over its rate) while trending_, else -1.
    int32_t trendSeconds_ = -1;
    scope::TriggerMode triggerMode_ = scope::TriggerMode::Auto;
    scope::TriggerType triggerType_ = scope::TriggerType::Edge;
    DataStatus health_ = DataStatus::NoData;
    bool waiting_ = false, statusHeld_ = false,automatic_=false,calibrating_=false;
    static constexpr uint32_t TrigdHoldMs = 500;
    bool trigd_ = false, newFrame_ = false; // newFrame_: status() called by update() for a record not shown before.
    uint32_t trigdAt_ = 0;
    // The record update() was last given (sequence and timestamp, as Model::project tells records apart):
    // the same record passed again (a pan, a data refresh, a HOLD or status change) is no new frame.
    bool frameSeen_ = false;
    uint32_t frameSequence_ = 0;
    uint64_t frameTimestampUs_ = 0;
};
} // namespace openosc::ui
