#pragma once
#include "openosc/measurement/dmm/log_settings.hpp"
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// Measurement > Session log (GitHub #144, docs/DMM_SESSION_LOGGING.md): start or stop a session, what
// the next one records, and the saved sessions. Mode, interval, duration and the event band are choices
// from dmm::LogSettings' lists; the estimate says what the plan
// writes (an interval plan too long for one file is said, and refused at start). The controller takes
// each committed field at once and saves it; a running session keeps the settings it started with.
class MeterLogProperties final : public PropertyList {
public:
    enum Row { Action, Mode, Interval, Duration, Threshold, Estimate, Sessions, Count };
    struct State { // Small: the panel is a member of the controller (main()'s frame).
        bool recording = false, committing = false;
        uint32_t elapsedS = 0;
    };
    MeterLogProperties() = default;
    MeterLogProperties(const dmm::LogSettings &settings, const State &state) : settings_(settings), state_(state) {}
    const dmm::LogSettings &settings() const { return settings_; }
    const State &state() const { return state_; }
    const char *title() const override { return "SESSION LOG"; }
    const char *notice() const override;
    unsigned count() const override { return Count; }
    PropertyKind kind(unsigned row) const override;
    void name(unsigned row, char *output, size_t capacity) const override;
    void value(unsigned row, char *output, size_t capacity) const override;
    unsigned options(unsigned row) const override;
    unsigned option(unsigned row) const override;
    void optionName(unsigned row, unsigned index, char *output, size_t capacity) const override;
    bool choose(unsigned row, unsigned index) override;
    // "1 s", "30 s", "2 min", "1 h", "7 d"; "00:12:34", "26:00:05" (hours past a day keep counting);
    // "5.0 %" (per mille).
    static void span(uint32_t seconds, char *output, size_t capacity);
    static void percent(uint32_t permille, char *output, size_t capacity);
    static void clock(uint64_t microseconds, char *output, size_t capacity);

private:
    dmm::LogSettings settings_;
    State state_;
};
} // namespace openosc::ui
