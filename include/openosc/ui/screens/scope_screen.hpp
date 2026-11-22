#pragma once
#include <cstring>
#include "openosc/acquisition/readouts.hpp"
#include "openosc/ui/components/readout_text.hpp"
#include "openosc/acquisition/counter_window.hpp"
#include "openosc/generator/waveform.hpp"
#include "openosc/ui/scope/cursor_control.hpp"
#include "openosc/ui/screens/instrument_screen.hpp"

namespace openosc::ui {
class ScopeScreen final : public InstrumentScreen {
public:
    using InstrumentScreen::InstrumentScreen;
    void activate(const ApplicationState &state) override;
    void update(const scope::FrameView &frame, bool held, unsigned channel, DataStatus status);
    void status(DataStatus status, bool held);
    void settings(const scope::FrameMetadata &next, unsigned channel);
    // panReverse: Settings > Display > Pan direction, for Zoom & Pan's Left/Right hint.
    void context(unsigned channel, plot::Focus focus, bool panReverse = false);
    void generator(const generator::Snapshot &output, bool captureHalved);
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

private:
    scope::FrameMetadata next_; // The acquisition settings of the next capture.
    scope::ReadoutSelection measurements_;
    scope::CounterSnapshot counters_;
    bool countersValid_=false, readoutsPainted_=false;
    void cards();
    void select(unsigned channel); // The cards' channel, its trace drawn in front (#81).
    unsigned channel_ = 0;
    bool held_ = false, valid_ = false;
    plot::Focus focus_ = plot::Focus::Scale;
    bool panReverse_ = false;
    void zoomNote(); // Zoom & Pan's factor and arrows in the plot's note (the GEN note returns on the next pass).
    const char *status_ = nullptr;
    scope::TriggerMode triggerMode_ = scope::TriggerMode::Auto;
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
