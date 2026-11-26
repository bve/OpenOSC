#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/packet.hpp"
#include "openosc/measurement/dmm/result.hpp"
#include <cctype>
#include <cstdio>
#include <cstring>

namespace openosc::dmm {
char Decoder::character(uint8_t segment) {
    static constexpr uint8_t patterns[] = {0,    0x0a, 0x24, 0x2f, 0x4e, 0x61, 0x67, 0x8a, 0x8f, 0xad,
                                           0xc7, 0xcf, 0xe1, 0xe4, 0xe5, 0xe7, 0xeb, 0xee, 0xef};
    static constexpr char chars[] = " 1-d4Lb73259cfe60a8";
    for (unsigned i = 0; i < sizeof patterns; ++i)
        if (segment == patterns[i])
            return chars[i];
    return '-';
}
Decoder::Digits Decoder::positions(const char *characters) {
    bool digit = false, dash = false, other = false;
    for (; *characters; ++characters) {
        if (*characters >= '0' && *characters <= '9') digit = true;
        else if (*characters == '-') dash = true;
        else if (*characters != '.' && *characters != ' ') other = true;
    }
    return other || (digit && dash) ? Digits::Other : digit ? Digits::Number : Digits::Blank;
}
Decoder::Digits Decoder::digits(const char *text) {
    // The sign: a leading '-' before a digit, or before a point and a digit ("-.1234": the sign bit
    // over a blank top position). "--." and "-.-" have none.
    const char *rest = text + 1;
    const bool sign = *text == '-' && (std::isdigit(uint8_t(*rest)) || (*rest == '.' && std::isdigit(uint8_t(rest[1]))));
    return positions(sign ? rest : text);
}
const char *Decoder::recordText(const Result &result) {
    if (result.primary.valid()) return result.mainText;
    NumericValue again;
    classify(result.mainText, result.rawUnit, digits(result.mainText), again);
    return again.valid() ? "?" : result.mainText;
}
void Decoder::classify(const char *text, const char *rawUnit, Digits digits, NumericValue &primary) {
    primary = {};
    NumericValue unitType;
    if (UnitParser::parse("0", rawUnit, unitType))
        primary.quantity = unitType.quantity;
    if (std::strchr(text, 'L')) primary.status = DataStatus::Overload;
    else if (digits == Digits::Blank) primary.status = DataStatus::NoReading;
    else if (digits == Digits::Other || !UnitParser::parse(text, rawUnit, primary)) // Writes only on success.
        primary.status = DataStatus::Unsupported;
}
RangeMode Decoder::range(bool autoBit, bool manualBit, const Result &result) {
    if (autoBit) return RangeMode::Auto;
    if (manualBit) return RangeMode::Manual;
    const auto status = result.primary.status;
    // The range mode is metadata apart from the number: a value, OL or the DMM's "no number yet"
    // (NoReading, which can last seconds on a large C) of a known function and unit; never Unsupported.
    const bool reading = status == DataStatus::Ok || status == DataStatus::Overload || status == DataStatus::NoReading;
    const auto function = Functions::identify(result);
    return reading && Functions::ranged(function) && result.primary.quantity == Functions::quantity(function) ? RangeMode::Manual
                                                                                                             : RangeMode::Unknown;
}
bool Decoder::number(const uint8_t *p, unsigned first, unsigned count, char *text, size_t capacity) {
    size_t written = 0;
    if (p[first] & 0x10)
        text[written++] = '-';
    for (unsigned i = 0; i < count; ++i) {
        uint8_t segment = p[first - i];
        if (i && (segment & 0x10)) {
            if (written + 1 >= capacity)
                return false;
            text[written++] = '.';
        }
        char c = character(segment & 0xef);
        if (c != ' ') {
            if (written + 1 >= capacity)
                return false;
            text[written++] = c;
        }
    }
    text[written] = 0;
    return true;
}
bool Decoder::decode(const uint8_t *packet, size_t bytes, Result &output) {
    if (!Packet::valid(packet, bytes))
        return false;
    const auto *p = packet + 1;
    Result candidate;
    unsigned points = 0;
    for (unsigned i = 0; i < 4; ++i) if (p[i] & 0x10) { candidate.decimals = i + 1; ++points; }
    if (points > 1) candidate.decimals = 255;
    if (!number(p, 4, 5, candidate.mainText, sizeof candidate.mainText))
        return false;
    candidate.kind = (p[10] & 0x10)          ? Kind::Dc
                     : (p[10] & 0x40)        ? Kind::Ac
                     : (p[15] & 4)           ? Kind::Capacitance
                     : (p[9] & 0xc0) == 0xc0 ? Kind::DiodeWithBeeper
                     : (p[9] & 0x80)         ? Kind::Diode
                     : (p[9] & 0x40)         ? Kind::Continuity
                     : (p[15] & 0x40)        ? Kind::Resistance
                                             : Kind::Unknown;
    candidate.peerRelative = p[9] & 0x20;
    candidate.trueRms = p[10] & 0x40;
    candidate.packetHold = p[10] & 0x80;
    candidate.vfc = Functions::VfcModel && (p[10] & 8); // Also the MANU bit of range() below, as the factory reads it.
    candidate.currentInput = p[15] & 1;
    struct Flag {
        unsigned byte;
        uint8_t mask;
        char character;
    };
    static constexpr Flag flags[] = {{14, 64, 'n'}, {15, 8, 'm'}, {14, 128, 'u'}, {15, 4, 'F'},
                                     {15, 2, 'V'},  {15, 1, 'A'}, {15, 16, 'M'},  {15, 32, 'K'},
                                     {15, 64, '&'}, {14, 16, 'x'}};
    unsigned length = 0;
    for (auto f : flags)
        if (p[f.byte] & f.mask)
            candidate.rawUnit[length++] = f.character;
    std::strcpy(candidate.unit, candidate.rawUnit);
    if (!std::strcmp(candidate.rawUnit, "&") || !std::strcmp(candidate.rawUnit, "K&") ||
        !std::strcmp(candidate.rawUnit, "M&")) {
        const char *prefix = !std::strcmp(candidate.rawUnit, "K&")   ? "k"
                             : !std::strcmp(candidate.rawUnit, "M&") ? "M"
                                                                     : "";
        std::snprintf(candidate.unit, sizeof candidate.unit, "%sOhm", prefix);
    }
    // The five digit positions as segments (bit 4, the point or the sign, masked): the factory's
    // record+60 magnitude from their digits, and what they hold for classify (a dash there is a
    // segment, never the sign).
    char characters[6]{};
    for (unsigned i = 0; i < 5; ++i) {
        const char c = characters[i] = character(p[4 - i] & 0xef);
        if (c >= '0' && c <= '9')
            candidate.rawMagnitude = candidate.rawMagnitude * 10 + unsigned(c - '0');
    }
    classify(candidate.mainText, candidate.rawUnit, positions(characters), candidate.primary);
    if (candidate.primary.status == DataStatus::Overload)
        candidate.rawMagnitude = 66000;
    if (candidate.trueRms) {
        char numberText[16];
        if (!number(p, 8, 4, numberText, sizeof numberText))
            return false;
        const char *unit = p[9] & 1 ? "KHz" : "Hz";
        std::snprintf(candidate.secondaryText, sizeof candidate.secondaryText, "%s%s", numberText, unit);
        if (!UnitParser::parse(numberText, unit, candidate.secondary))
            candidate.secondary.status = DataStatus::Unsupported;
    }
    candidate.range = range(p[10] & 4, p[10] & 8, candidate);
    output = candidate;
    return true;
}
} // namespace openosc::dmm
