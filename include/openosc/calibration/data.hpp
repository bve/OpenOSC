#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc {
// The offset PWM compare at a position from a row of 7 nodes at Y = 20, 40, ..., 140, the end
// segments continued past them (the factory's interpolator, 703s_main:08049BDC); false when a node
// is no valid compare (over 9999) or the position is outside 0..160.
bool interpolateCompare(const uint16_t (&row)[7], int position, uint16_t &compare);
// The factory record at 0x0801C000 comes in three layouts. Long (the factory UI writer 0803D548): 168
// offset words, gain words at +336/+338 (gain x 100, 0: never written), the interleave correction
// float at +340 and the CRC-16 of bytes 0..343 at +344. Compact (seen on a ZT703S): the offsets, their
// CRC-16 at +336, zeros to the end. The CRC (0x8408, init 0xFFFF, no final XOR) leaves a zero
// residue, so a compact record also passes the long check, with CRC 0 and its own CRC read as CH1's
// gain. When that CRC could itself be a gain word (100..199), the bytes do not tell the two apart:
// Ambiguous, read as before (long, identity 0); only an explicit boot policy (scope
// factory_record_format, docs/ZERO_CALIBRATION.md) decides it, on outside evidence.
// MKR (seen in an MKR-MK02 factory boot image): the offsets, two gain words at +336/+338, the CRC-16
// of bytes 0..339 at +340; no correction (0); bytes 342..345 are not fields (that image holds
// 02 00 00 02 there). Its identity is that CRC. Auto takes it only when no older layout is valid,
// with both gain words 100..199 and every compare node <= 9999. With zeros after its CRC the same
// zero residue makes it long-valid too (long CRC 0):
// Ambiguous again, read as long; the explicit policy mkr reads it as MKR. The MKR application itself
// (V1.6.12) reads the long layout and falls back to built-in defaults: this decoder is OpenOSC's.
// Values keep their numbers (JSON and SCPI): Mkr is appended.
enum class FactoryRecordPolicy : uint8_t { Auto, Long, Compact, Mkr };
enum class FactoryRecordFormat : uint8_t { Long, Compact, Ambiguous, Mkr };
// Upper-case names (SCPI SYST:CAL:FACT:FORM?, the Recovery view).
inline const char *factoryRecordName(FactoryRecordPolicy policy) {
    return policy == FactoryRecordPolicy::Long ? "LONG" : policy == FactoryRecordPolicy::Compact ? "COMPACT"
         : policy == FactoryRecordPolicy::Mkr ? "MKR" : "AUTO";
}
inline const char *factoryRecordName(FactoryRecordFormat format) {
    return format == FactoryRecordFormat::Compact ? "COMPACT" : format == FactoryRecordFormat::Ambiguous ? "AMBIGUOUS"
         : format == FactoryRecordFormat::Mkr ? "MKR" : "LONG";
}
class CalibrationData {
  public:
    static constexpr size_t RecordBytes = 346;
    static constexpr uint16_t DefaultGainWord = 120; // An unwritten gain word (0): the factory's 1.20 (0803D618).
    // The one decoder of the record (the application and Recovery). False (and nothing changed) for a
    // wrong length or CRC, a non-finite correction, or a layout the policy rules out.
    bool load(const uint8_t *bytes, size_t length, FactoryRecordPolicy policy = FactoryRecordPolicy::Auto);
    bool valid() const { return valid_; }
    // The CRC of the selected layout. User calibration profiles are bound to this identity.
    uint16_t crc() const { return crc_; }
    FactoryRecordFormat format() const { return format_; }
    // A channel's gain word as decoded (0: not written, also every compact record; MKR: its two words).
    uint16_t gainWord(unsigned channel) const { return channel < 2 ? gains_[channel] : 0; }
    // Whether the record gives the channel a scale: a gain word 100..199, or 0 (the default 1.20).
    bool factoryScale(unsigned channel) const;
    bool offsetCompare(unsigned channel, unsigned range, int position, uint16_t &compare) const;
    // The factory's 7 compare nodes of a channel and range 0..10 (row 11 duplicates range 10 and is
    // never used).
    bool offsetRow(unsigned channel, unsigned range, uint16_t (&row)[7]) const;
    // The channel's factory gain (the word / 100, 0: 1.20); 0 without a valid record. Not a scale on
    // its own: factoryScale() says whether it is one.
    double displayGain(unsigned channel) const;
    // The factory transfer of a channel at a V/div and analog position. displayGain (absolute, as the
    // factory's 1..1.99; 0: this record's calibration gain) changes voltsPerCode only: the ADC zero
    // code at the offset PWM of that position stays the calibration gain's.
    bool scopeScale(unsigned channel,uint32_t millivoltsPerDivision,int position,double& voltsPerCode,double& zeroCode,
                    double displayGain=0) const;
    // The factory transfer itself for a calibration gain (absolute, 1.00..1.99): the one formula
    // scopeScale and ScopeCalibration's nominal defaults (no record) share. False out of its bounds.
    static bool transfer(double gain, uint32_t millivoltsPerDivision, int position, double displayGain,
                         double &voltsPerCode, double &zeroCode);
    // Record +0x154 (float32, default -1.0): the factory correction added to the HIGH ADC's points of
    // an interleaved record (0804E640); an enabled service offset (InterleaveProfile) replaces it.
    // 0 for a compact or an MKR record (none stored).
    float interleaveCorrection() const { return correction_; }

  private:
    uint16_t offsets_[2][12][7]{}, gains_[2]{}, crc_ = 0;
    float correction_ = 0;
    FactoryRecordFormat format_ = FactoryRecordFormat::Long;
    bool valid_ = false;
};
class ParameterContainer {
  public:
    // Read-only factory container validation; OpenOSC writes settings to external JSON.
    static bool valid(const uint8_t *bytes, size_t length);
};
} // namespace openosc
