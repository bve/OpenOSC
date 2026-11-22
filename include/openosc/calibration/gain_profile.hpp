#pragma once
#include "openosc/calibration/data.hpp"
#include "openosc/calibration/zero_profile.hpp"

namespace openosc::calibration {
// The user's gain: per channel and range a factor on the factory's volts per code (1: the
// factory's), measured against a reference (GainCalibration). It never moves the zero code: the
// ADC zero belongs to the offset PWM, not to the gain. Unmeasured cells stay 1.
struct GainProfile {
    static constexpr unsigned Channels = ZeroProfile::Channels, Ranges = ZeroProfile::Ranges;
    // Beyond these a measurement corrects no working front end (a wrong range, probe or reference).
    static constexpr double MinimumGain = .8, MaximumGain = 1.25;
    uint16_t factoryCrc = 0;
    bool enabled = false;
    float gain[Channels][Ranges];
    GainProfile() { for (auto &channel : gain) for (auto &factor : channel) factor = 1; }
    static bool allowed(double factor) { return factor >= MinimumGain && factor <= MaximumGain; } // NaN: no.
    // Bound to this factory record, every factor finite and allowed.
    bool valid(const CalibrationData &factory) const;
    // The factor in force for a channel and range: 1 when disabled or out of the table.
    double factor(unsigned channel, unsigned range) const;
    // Whether one measured cell can be saved (enabled) leaving every other cell in force as it is:
    // always for an enabled profile; a disabled one (Use factory gain) only with every other cell at
    // 1, since enabling it would bring its saved factors back (GitHub #80: no other channel or range
    // changes, no saved value is erased).
    bool cellSavable(unsigned channel, unsigned range) const;
    // This profile with one measured cell: enabled, bound to `crc`, every other cell as it is.
    GainProfile withCell(uint16_t crc, unsigned channel, unsigned range, double factor) const;
};
// Where the user gain is kept (GainFile on the calibration disk).
class GainStore {
public:
    virtual ~GainStore() = default;
    virtual bool available() const = 0;
    virtual bool loaded() const = 0; // values() are what the disk holds.
    virtual const GainProfile &values() const = 0;
    virtual bool save(const GainProfile &values) = 0;
};
} // namespace openosc::calibration
