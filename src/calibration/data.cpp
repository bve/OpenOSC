#include "openosc/calibration/data.hpp"
#include "openosc/data/binary.hpp"
#include "openosc/data/crc16.hpp"
#include <cmath>
#include <cstring>

namespace openosc {
namespace {
constexpr unsigned OffsetBytes = 336, LongDataBytes = 344, MkrDataBytes = 340;
bool plausibleGain(uint16_t word) { return word >= 100 && word <= 199; }
// Every offset word a compare the interpolator takes (interpolateCompare: <= 9999).
bool plausibleNodes(const uint8_t *bytes) {
    for (unsigned i = 0; i < OffsetBytes; i += 2) if (Binary::readLe(bytes + i, 2) > 9999) return false;
    return true;
}
} // namespace
bool CalibrationData::load(const uint8_t *bytes, size_t length, FactoryRecordPolicy policy) {
    if (!bytes || length != RecordBytes) return false;
    const bool longValid = Crc16::compute(bytes, LongDataBytes) == Binary::readLe(bytes + LongDataBytes, 2);
    bool tailZero = true;
    for (unsigned i = OffsetBytes + 2; i < RecordBytes; ++i) tailZero = tailZero && !bytes[i];
    const uint16_t prefixWord = Binary::readLe(bytes + OffsetBytes, 2);
    const bool compactValid = tailZero && Crc16::compute(bytes, OffsetBytes) == prefixWord; // Implies longValid.
    const uint16_t secondWord = Binary::readLe(bytes + OffsetBytes + 2, 2), mkrCrc = Binary::readLe(bytes + MkrDataBytes, 2);
    const bool mkrValid = Crc16::compute(bytes, MkrDataBytes) == mkrCrc;
    // What Auto takes as MKR: its CRC, both gains written and in the factory's band, every node a compare.
    const bool mkrPlausible = mkrValid && plausibleGain(prefixWord) && plausibleGain(secondWord) && plausibleNodes(bytes);
    bool padding = true; // Zeros after the MKR CRC: then also long-valid with CRC 0 (the zero residue).
    for (unsigned i = MkrDataBytes + 2; i < RecordBytes; ++i) padding = padding && !bytes[i];
    CalibrationData candidate;
    switch (policy) {
    case FactoryRecordPolicy::Auto:
        if (compactValid) candidate.format_ = plausibleGain(prefixWord) ? FactoryRecordFormat::Ambiguous : FactoryRecordFormat::Compact;
        else if (longValid) candidate.format_ = mkrPlausible && padding ? FactoryRecordFormat::Ambiguous : FactoryRecordFormat::Long;
        else if (mkrPlausible) candidate.format_ = FactoryRecordFormat::Mkr;
        else return false;
        break;
    case FactoryRecordPolicy::Long:
        if (!longValid) return false;
        candidate.format_ = FactoryRecordFormat::Long;
        break;
    case FactoryRecordPolicy::Compact:
        if (!compactValid) return false;
        candidate.format_ = FactoryRecordFormat::Compact;
        break;
    case FactoryRecordPolicy::Mkr: // Outside evidence: its CRC alone (gains outside the band give no scale).
        if (!mkrValid) return false;
        candidate.format_ = FactoryRecordFormat::Mkr;
        break;
    default: return false;
    }
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned row = 0; row < 12; ++row)
            for (unsigned column = 0; column < 7; ++column)
                candidate.offsets_[c][row][column] =
                    Binary::readLe(bytes + 2 * ((c * 12 + row) * 7 + column), 2);
    if (candidate.format_ == FactoryRecordFormat::Compact) {
        candidate.crc_ = prefixWord; // No gain words (0: the default) and no correction.
    } else if (candidate.format_ == FactoryRecordFormat::Mkr) {
        candidate.gains_[0] = prefixWord; candidate.gains_[1] = secondWord;
        candidate.crc_ = mkrCrc; // Its identity; no correction (0). Bytes past the CRC are not read.
    } else { // Long and Ambiguous both use the long layout for the gain, correction, and identity.
        candidate.gains_[0] = prefixWord;
        candidate.gains_[1] = secondWord;
        uint32_t raw = Binary::readLe(bytes + OffsetBytes + 4, 4);
        std::memcpy(&candidate.correction_, &raw, sizeof raw);
        if (!std::isfinite(candidate.correction_))
            return false;
        candidate.crc_ = Binary::readLe(bytes + LongDataBytes, 2);
    }
    candidate.valid_ = true;
    *this = candidate;
    return true;
}
bool CalibrationData::factoryScale(unsigned channel) const {
    return valid_ && channel < 2 && (!gains_[channel] || plausibleGain(gains_[channel]));
}
bool interpolateCompare(const uint16_t (&row)[7], int position, uint16_t &output) {
    if (position < 0 || position > 160)
        return false;
    int segment = position <= 20 ? 0 : position >= 140 ? 5 : (position - 20) / 20;
    const int32_t first = row[segment], second = row[segment + 1];
    if (first > 9999 || second > 9999)
        return false;
    int32_t compare = first + (second - first) * (position - (20 + 20 * segment)) / 20;
    output = compare < 0 ? 0 : compare > 9999 ? 9999 : compare;
    return true;
}
bool CalibrationData::offsetRow(unsigned channel, unsigned range, uint16_t (&row)[7]) const {
    if (!valid_ || channel > 1 || range > 10)
        return false;
    for (unsigned i = 0; i < 7; ++i) row[i] = offsets_[channel][range][i];
    return true;
}
bool CalibrationData::offsetCompare(unsigned channel, unsigned range, int position, uint16_t &output) const {
    uint16_t row[7];
    return offsetRow(channel, range, row) && interpolateCompare(row, position, output);
}
double CalibrationData::displayGain(unsigned channel) const {
    if (!valid_ || channel > 1)
        return 0;
    return (gains_[channel] ? gains_[channel] : DefaultGainWord) / 100.0;
}
bool ParameterContainer::valid(const uint8_t *bytes, size_t length) {
    return bytes && (length == 88 || length == 614) &&
           Crc16::compute(bytes, length - 2) == Binary::readLe(bytes + length - 2, 2);
}
} // namespace openosc
