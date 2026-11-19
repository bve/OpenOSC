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
    void context(unsigned channel, plot::Focus focus);
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
    unsigned channel_ = 0;
    bool held_ = false, valid_ = false;
    plot::Focus focus_ = plot::Focus::Scale;
    const char *status_ = nullptr;
    scope::TriggerMode triggerMode_ = scope::TriggerMode::Auto;
    DataStatus health_ = DataStatus::NoData;
    bool waiting_ = false, statusHeld_ = false,automatic_=false,calibrating_=false;
};
} // namespace openosc::ui
