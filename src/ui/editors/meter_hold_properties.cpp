#include "openosc/ui/editors/meter_hold_properties.hpp"
#include <cstdio>

namespace openosc::ui {
using Settings = dmm::HoldSettings;
const char *MeterHoldProperties::notice() const {
    switch (settings_.mode) {
    case dmm::HoldMode::Delayed: return "HOLD: countdown, then one reading";
    case dmm::HoldMode::Stable: return "HOLD: waits for one stable reading";
    case dmm::HoldMode::Auto: return "Auto HOLD skips near-zero V/A/F";
    default: return "HOLD: the reading at the key";
    }
}
void MeterHoldProperties::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"Mode", "Delay", "Stable time", "Stable band", "Rearm threshold", "Min readings", "Zero threshold"};
    std::snprintf(output, capacity, "%s", row < Count ? names[row] : "");
}
void MeterHoldProperties::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    std::snprintf(output, capacity, "%s", row == Mode ? Settings::name(dmm::HoldMode(index)) : "");
}
void MeterHoldProperties::value(unsigned row, char *output, size_t capacity) const {
    const auto &s = settings_;
    switch (row) {
    case Mode: PropertyList::value(row, output, capacity); break;
    case Delay: std::snprintf(output, capacity, "%lu s", (unsigned long)(s.delayMs / 1000)); break;
    case StableTime: std::snprintf(output, capacity, "%lu ms", (unsigned long)s.stableMs); break;
    case Band: std::snprintf(output, capacity, "%lu digits", (unsigned long)s.bandDigits); break;
    case Rearm: std::snprintf(output, capacity, "%lu digits", (unsigned long)s.rearmDigits); break;
    case Readings: std::snprintf(output, capacity, "%lu", (unsigned long)s.minReadings); break;
    case Zero: std::snprintf(output, capacity, "%lu digits", (unsigned long)s.zeroDigits); break;
    default: if (capacity) output[0] = 0; break;
    }
}
bool MeterHoldProperties::choose(unsigned row, unsigned index) {
    if (row != Mode || index >= unsigned(dmm::HoldMode::Count)) return false;
    settings_.mode = dmm::HoldMode(index);
    return true;
}
int MeterHoldProperties::coarseStep(unsigned row) const {
    switch (row) {
    case Delay: return 10;      // 10 s
    case StableTime: return 10; // 1 s
    case Band: case Rearm: case Zero: return 10;
    case Readings: return 4;
    default: return 1;
    }
}
int32_t MeterHoldProperties::number(unsigned row) const {
    const auto &s = settings_;
    switch (row) {
    case Delay: return int32_t(s.delayMs / Settings::DelayStepMs);
    case StableTime: return int32_t(s.stableMs / Settings::StableStepMs);
    case Band: return int32_t(s.bandDigits);
    case Rearm: return int32_t(s.rearmDigits);
    case Readings: return int32_t(s.minReadings);
    case Zero: return int32_t(s.zeroDigits);
    default: return 0;
    }
}
bool MeterHoldProperties::limits(unsigned row, int32_t &low, int32_t &high) {
    switch (row) {
    case Delay: low = Settings::MinDelayMs / Settings::DelayStepMs; high = Settings::MaxDelayMs / Settings::DelayStepMs; return true;
    case StableTime: low = Settings::MinStableMs / Settings::StableStepMs; high = Settings::MaxStableMs / Settings::StableStepMs; return true;
    case Band: low = Settings::MinBand; high = Settings::MaxBand; return true;
    case Rearm: low = Settings::MinRearm; high = Settings::MaxRearm; return true;
    case Readings: low = Settings::MinReadings; high = Settings::MaxReadings; return true;
    case Zero: low = 0; high = Settings::MaxZero; return true;
    default: return false;
    }
}
// Clamped to the row's range, never wrapped; the pair through HoldSettings::band/rearm.
void MeterHoldProperties::setNumber(unsigned row, int32_t value) {
    int32_t low, high;
    if (!limits(row, low, high)) return;
    const uint32_t v = uint32_t(value < low ? low : value > high ? high : value);
    auto &s = settings_;
    switch (row) {
    case Delay: s.delayMs = v * Settings::DelayStepMs; break;
    case StableTime: s.stableMs = v * Settings::StableStepMs; break;
    case Band: s.band(v); break;
    case Rearm: s.rearm(v); break;
    case Readings: s.minReadings = v; break;
    case Zero: s.zeroDigits = v; break;
    default: break;
    }
}
void MeterHoldProperties::adjust(unsigned row, int steps) { setNumber(row, number(row) + steps); }
} // namespace openosc::ui
