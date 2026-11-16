#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc {
// The offset PWM compare at a position from a row of 7 nodes at Y = 20, 40, ..., 140, the end
// segments continued past them (the factory's interpolator, 703s_main:08049BDC); false when a node
// is no valid compare (over 9999) or the position is outside 0..160.
bool interpolateCompare(const uint16_t (&row)[7], int position, uint16_t &compare);
class CalibrationData {
  public:
    static constexpr size_t RecordBytes = 346;
    bool load(const uint8_t *bytes, size_t length);
    bool valid() const { return valid_; }
    uint16_t crc() const { return crc_; }
    uint16_t gainWord(unsigned channel) const { return channel < 2 ? gains_[channel] : 0; } // As stored.
    bool offsetCompare(unsigned channel, unsigned range, int position, uint16_t &compare) const;
    // The factory's 7 compare nodes of a channel and range 0..10 (row 11 duplicates range 10 and is
    // never used).
    bool offsetRow(unsigned channel, unsigned range, uint16_t (&row)[7]) const;
    double displayGain(unsigned channel) const; // The gain word / 100; unwritten (0): the factory default 1.20.
    // The factory transfer of a channel at a V/div and analog position. displayGain (absolute, as the
    // factory's 1..1.99; 0: this record's calibration gain) changes voltsPerCode only: the ADC zero
    // code at the offset PWM of that position stays the calibration gain's.
    bool scopeScale(unsigned channel,uint32_t millivoltsPerDivision,int position,double& voltsPerCode,double& zeroCode,
                    double displayGain=0) const;
    // Record +0x154 (float32, default -1.0): the factory correction added to the HIGH ADC's points of
    // an interleaved record (0804E640); an enabled service offset (InterleaveProfile) replaces it.
    float interleaveCorrection() const { return correction_; }

  private:
    uint16_t offsets_[2][12][7]{}, gains_[2]{}, crc_ = 0;
    float correction_ = 0;
    bool valid_ = false;
};
class ParameterContainer {
  public:
    // Read-only factory container validation; OpenOSC writes settings to external JSON.
    static bool valid(const uint8_t *bytes, size_t length);
};
} // namespace openosc
