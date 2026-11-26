#include "openosc/ui/editors/meter_log_properties.hpp"
#include <cstdio>

namespace openosc::ui {
using Settings = dmm::LogSettings;
const char *MeterLogProperties::notice() const {
    if (state_.recording) return "Recording: changes apply to the next session";
    return settings_.mode() == dmm::LogMode::Event ? "A record each time the reading leaves its band"
                                                 : "A row each interval: min, max, avg";
}
PropertyKind MeterLogProperties::kind(unsigned row) const {
    switch (row) {
    case Action: return PropertyKind::Action;
    case Mode: case Interval: case Duration: case Threshold: return PropertyKind::Choice;
    case Sessions: return PropertyKind::Page;
    default: return PropertyKind::Info;
    }
}
void MeterLogProperties::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"", "Mode", "Interval", "Duration", "Event band", "Estimate", "Saved sessions"};
    if (row == Action)
        std::snprintf(output, capacity, "%s", state_.recording ? "Stop session" : state_.committing ? "Saving session" : "Start session");
    else std::snprintf(output, capacity, "%s", row < Count ? names[row] : "");
}
unsigned MeterLogProperties::options(unsigned row) const {
    switch (row) {
    case Mode: return unsigned(dmm::LogMode::Count);
    case Interval: return Settings::IntervalCount;
    case Duration: return Settings::DurationCount;
    case Threshold: return Settings::BandCount;
    default: return 0;
    }
}
unsigned MeterLogProperties::option(unsigned row) const {
    switch (row) {
    case Mode: return unsigned(settings_.mode());
    case Interval: return settings_.intervalIndex();
    case Duration: return settings_.durationIndex();
    case Threshold: return settings_.bandIndex();
    default: return 0;
    }
}
void MeterLogProperties::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    switch (row) {
    case Mode: std::snprintf(output, capacity, "%s", Settings::name(dmm::LogMode(index < options(row) ? index : 0))); break;
    case Interval: span(Settings::Intervals[index < Settings::IntervalCount ? index : 0], output, capacity); break;
    case Duration: span(Settings::Durations[index < Settings::DurationCount ? index : 0], output, capacity); break;
    case Threshold: percent(Settings::Bands[index < Settings::BandCount ? index : 0], output, capacity); break;
    default: PropertyList::optionName(row, index, output, capacity); break;
    }
}
void MeterLogProperties::value(unsigned row, char *output, size_t capacity) const {
    if (!capacity) return;
    switch (row) {
    case Action: {
        if (!state_.recording && !state_.committing) { std::snprintf(output, capacity, "Ready"); break; }
        char time[24];
        clock(uint64_t(state_.elapsedS) * 1000000, time, sizeof time);
        std::snprintf(output, capacity, "%s %s", state_.recording ? "REC" : "Saving", time);
        break;
    }
    case Estimate: {
        if (settings_.mode() == dmm::LogMode::Event) {
            std::snprintf(output, capacity, "Up to %lu KB", static_cast<unsigned long>(settings_.reserveBytes() / 1024));
            break;
        }
        const uint32_t rows = settings_.plannedRows();
        if (!settings_.fits()) {
            std::snprintf(output, capacity, "Too long: %lu rows max", static_cast<unsigned long>(Settings::MaximumRows));
            break;
        }
        std::snprintf(output, capacity, "%lu rows, %lu KB", static_cast<unsigned long>(rows),
                      static_cast<unsigned long>((settings_.reserveBytes() + 1023) / 1024));
        break;
    }
    case Sessions: output[0] = 0; break;
    default: PropertyList::value(row, output, capacity); break;
    }
}
bool MeterLogProperties::choose(unsigned row, unsigned index) {
    if (index >= options(row)) return false;
    switch (row) {
    case Mode: return settings_.mode(dmm::LogMode(index));
    case Interval: return settings_.intervalIndex(index);
    case Duration: return settings_.durationIndex(index);
    case Threshold: return settings_.bandIndex(index);
    default: return false;
    }
}
void MeterLogProperties::span(uint32_t s, char *output, size_t capacity) {
    const auto u = [](uint32_t v) { return static_cast<unsigned long>(v); };
    if (s < 60) std::snprintf(output, capacity, "%lu s", u(s));
    else if (s < 3600) std::snprintf(output, capacity, "%lu min", u(s / 60));
    else if (s < 86400 || s % 86400) std::snprintf(output, capacity, "%lu h", u(s / 3600));
    else std::snprintf(output, capacity, "%lu d", u(s / 86400));
}
void MeterLogProperties::percent(uint32_t permille, char *output, size_t capacity) {
    std::snprintf(output, capacity, "%lu.%lu %%", static_cast<unsigned long>(permille / 10), static_cast<unsigned long>(permille % 10));
}
void MeterLogProperties::clock(uint64_t us, char *output, size_t capacity) {
    const uint64_t s = us / 1000000;
    std::snprintf(output, capacity, "%02lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600),
                  static_cast<unsigned long>(s / 60 % 60), static_cast<unsigned long>(s % 60));
}
} // namespace openosc::ui
