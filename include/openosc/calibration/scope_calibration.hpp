#pragma once
#include "openosc/calibration/pwm_profile.hpp"
#include "openosc/calibration/gain_profile.hpp"
#include "openosc/calibration/interleave_profile.hpp"

namespace openosc::calibration {
// The offset PWM the analog front end applies, one runtime view over the read-only factory record
// (never copied), the user's hardware-zero profile and a calibration's trial: for each channel a
// trial at exactly its range and node wins, then the profile's nodes (when enabled), then the
// factory's, interpolated alike (interpolateCompare). generation() changes with every change of
// that answer, so a front end with the same settings still applies a new compare.
// It also holds the user's gain (volts per code only) and the ZT706S service interleave offset;
// neither touches the hardware, so neither changes generation(): captures take them when they are
// described (ChannelScale), so a record keeps the scale it was taken with.
//
// Without a factory record (none at 0x0801C000, or refused: CRC, layout) the Scope still runs, on the
// factory firmware's built-in defaults (startup 0803D618 / 0803DEF8 / 0803CD54, the same on ZT703S,
// ZT706S and MKR): one offset row for both channels and every range, the gain 1.20, the interleave
// correction -1.0. Only here, never in the record: CalibrationData stays invalid (SCPI, Recovery,
// every profile bound to a real CRC), nothing is loaded, saved or calibrated against them, and the
// UI says the readings are approximate (nominal()).
class ScopeCalibration {
public:
    static constexpr uint16_t NominalRow[PwmProfile::Points] = {6500, 6100, 5600, 5200, 4700, 4300, 3800};
    static constexpr double NominalGain = CalibrationData::DefaultGainWord / 100.0; // 1.20
    static constexpr float NominalInterleave = -1.0f;
    explicit ScopeCalibration(const CalibrationData &factory) : factory_(factory) {}
    const CalibrationData &factory() const { return factory_; }
    // A real factory record: every calibration, profile and save needs it.
    bool valid() const { return factory_.valid(); }
    // No record: the offsets and the scale are the factory's built-in defaults, approximate.
    bool nominal() const { return !factory_.valid(); }
    uint32_t generation() const { return generation_; }
    bool offsetCompare(unsigned channel, unsigned range, int position, uint16_t &compare) const;
    // The node in force without a trial: the profile's when enabled, else the factory's (a real record only).
    bool node(unsigned channel, unsigned range, unsigned point, uint16_t &compare) const;
    // A hardware zero is in force: the software zero (scope_zero.json) is then not added, so the
    // two never correct the same offset twice.
    bool pwmActive() const { return pwm_.enabled; }
    // The software zero's correction a capture adds to its zero code: none under a hardware zero.
    double softwareZero(const ZeroProfile &software, unsigned channel, unsigned range, int position) const {
        return pwm_.enabled ? 0 : software.correction(channel, range, position);
    }
    const PwmProfile &pwm() const { return pwm_; }
    // The factory transfer of a channel at a (physical) range and position, its volts per code x
    // the user gain of that range, once; the zero code the factory transfer's (no gain moves it).
    // displayGain: an interleaved CH1's absolute frequency gain in place of the calibration gain
    // (0: the calibration gain), volts per code only.
    bool scale(unsigned channel, unsigned range, int position, double &voltsPerCode, double &zeroCode,
               double displayGain = 0) const;
    const GainProfile &gain() const { return gain_; }
    bool setGain(const GainProfile &profile); // An enabled profile must be valid; a disabled one is kept as values.
    // The HIGH ADC's offset of a new interleaved record: the service profile when enabled, else the
    // factory record's (0 when that is no allowed offset); without a record the factory default -1.0.
    float interleaveOffset() const;
    const InterleaveProfile &interleave() const { return interleave_; }
    bool setInterleave(const InterleaveProfile &profile);
    // An enabled profile must be valid for this factory record; a disabled one is kept as values.
    bool setPwm(const PwmProfile &profile);
    void setTrial(unsigned channel, unsigned range, unsigned point, uint16_t compare);
    void clearTrial();

private:
    struct Trial { bool active = false; unsigned range = 0, point = 0; uint16_t compare = 0; };
    const CalibrationData &factory_;
    PwmProfile pwm_;
    GainProfile gain_;
    InterleaveProfile interleave_;
    Trial trial_[PwmProfile::Channels];
    uint32_t generation_ = 1;
};
} // namespace openosc::calibration
