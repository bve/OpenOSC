#include "openosc/calibration/data.hpp"
#include "openosc/data/binary.hpp"
#include "openosc/data/crc16.hpp"
#include <cmath>
#include <cstring>

namespace openosc {
bool CalibrationData::load(const uint8_t *bytes, size_t length) {
    if (!bytes || length != RecordBytes || Crc16::compute(bytes, 344) != Binary::readLe(bytes + 344, 2))
        return false;
    CalibrationData candidate;
    for (unsigned c = 0; c < 2; ++c)
        for (unsigned row = 0; row < 12; ++row)
            for (unsigned column = 0; column < 7; ++column)
                candidate.offsets_[c][row][column] =
                    Binary::readLe(bytes + 2 * ((c * 12 + row) * 7 + column), 2);
    // The short record (seen on a ZT703S, 2026-11-16): the offsets, their CRC-16 at +336 and zeros to
    // the end; no gain words, no correction. It passes the check above by the CRC's residue (0 over
    // data and its own CRC, kept by the zero bytes, so the word at +344 matches), and read as the
    // long one its CRC word was CH1's gain: CH1 never had a scale. Here: both gains unwritten (the
    // factory default, displayGain), no correction, and the record's own CRC as its identity.
    bool tailZero = true;
    for (unsigned i = 338; i < RecordBytes; ++i) tailZero = tailZero && !bytes[i];
    const bool compact = tailZero && Crc16::compute(bytes, 336) == Binary::readLe(bytes + 336, 2);
    if (!compact) {
        candidate.gains_[0] = Binary::readLe(bytes + 336, 2);
        candidate.gains_[1] = Binary::readLe(bytes + 338, 2);
        uint32_t raw = Binary::readLe(bytes + 340, 4);
        std::memcpy(&candidate.correction_, &raw, sizeof raw);
        if (!std::isfinite(candidate.correction_))
            return false;
    }
    candidate.crc_ = Binary::readLe(bytes + (compact ? 336 : 344), 2);
    candidate.valid_ = true;
    *this = candidate;
    return true;
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
    return gains_[channel] ? gains_[channel] / 100.0 : 1.2;
}
bool ParameterContainer::valid(const uint8_t *bytes, size_t length) {
    return bytes && (length == 88 || length == 614) &&
           Crc16::compute(bytes, length - 2) == Binary::readLe(bytes + length - 2, 2);
}
} // namespace openosc
