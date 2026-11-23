#include "openosc/data/csv.hpp"
#include "openosc/data/decimal.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace openosc {
namespace {
bool quote(const char *input, size_t limit, char *output, size_t capacity) {
    size_t n = 0;
    if (capacity < 3)
        return false;
    output[n++] = '"';
    size_t i = 0;
    for (; i < limit && input[i]; ++i) {
        if (n + 3 >= capacity)
            return false;
        if (input[i] == '"')
            output[n++] = '"';
        output[n++] = input[i];
    }
    if (i == limit)
        return false;
    output[n++] = '"';
    output[n] = 0;
    return true;
}
} // namespace
const char *Csv::quantity(Quantity value) {
    switch (value) {
    case Quantity::Voltage:
        return "voltage";
    case Quantity::Current:
        return "current";
    case Quantity::Resistance:
        return "resistance";
    case Quantity::Capacitance:
        return "capacitance";
    case Quantity::Frequency:
        return "frequency";
    case Quantity::Temperature:
        return "temperature";
    default:
        return "unknown";
    }
}
namespace {
const char *unit(Quantity value) { // The SI unit of a value_si, as MeterFunctions names them.
    switch (value) {
    case Quantity::Voltage: return "V";
    case Quantity::Current: return "A";
    case Quantity::Resistance: return "Ohm";
    case Quantity::Capacitance: return "F";
    case Quantity::Frequency: return "Hz";
    case Quantity::Temperature: return "degC";
    default: return "";
    }
}
} // namespace
size_t Csv::dmm(const dmm::Result &result, bool hold, char *out, size_t capacity) {
    if (!out || !capacity)
        return 0;
    char unit[28], text[36], number[40]{};
    if (!quote(result.unit, sizeof result.unit, unit, sizeof unit) ||
        !quote(result.mainText, sizeof result.mainText, text, sizeof text))
        return 0;
    if (result.primary.valid() && !Decimal::number(result.primary.si, number, sizeof number))
        return 0;
    char timestamp[24];
    if (!Decimal::integer(result.timestampUs, timestamp, sizeof timestamp))
        return 0;
    const int n = std::snprintf(out, capacity, "%s,%lu,%u,%s,%s,%s,%s,%u,%u,%u\r\n", timestamp,
                                static_cast<unsigned long>(result.sequence), unsigned(result.primary.status),
                                quantity(result.primary.quantity), number, unit, text,
                                unsigned(result.packetHold), unsigned(hold), unsigned(result.peerRelative));
    return n > 0 && size_t(n) < capacity ? size_t(n) : 0;
}
size_t Csv::scope(const scope::FrameView &frame, size_t index, char *out, size_t capacity, const float *meter) {
    if (!out || !capacity || !frame.valid() || index >= frame.metadata().samples)
        return 0;
    char raw[2][8]{}, volts[2][40]{};
    for (unsigned c = 0; c < 2; ++c) {
        int16_t code;
        double value;
        if (frame.signedCode(static_cast<scope::Channel>(c), index, code))
            std::snprintf(raw[c], sizeof raw[c], "%d", int(code));
        if (frame.volts(static_cast<scope::Channel>(c), index, value) &&
            !Decimal::number(value, volts[c], sizeof volts[c]))
            return 0;
    }
    char time[48];
    if (!Decimal::fraction(uint64_t(index) * frame.metadata().samplesPerSecond.denominator,
                           frame.metadata().samplesPerSecond.numerator, time, sizeof time))
        return 0;
    char reading[40] = "";
    // A float's seven digits: more would print its binary rounding.
    if (meter && std::isfinite(*meter) && !Decimal::number(*meter, reading, sizeof reading, 7))
        return 0;
    const int n = std::snprintf(out, capacity, "%lu,%s,%s,%s,%s,%s%s%s\r\n", static_cast<unsigned long>(index),
                                time, raw[0], raw[1], volts[0], volts[1], meter ? "," : "", reading);
    return n > 0 && size_t(n) < capacity ? size_t(n) : 0;
}
size_t Csv::scopeMeter(const char *function, Quantity value, char *out, size_t capacity) {
    if (!out || !capacity || !function)
        return 0;
    const int n = std::snprintf(out, capacity, "# dmm: function=%s,quantity=%s,unit=%s\r\n", function, quantity(value),
                                unit(value));
    return n > 0 && size_t(n) < capacity ? size_t(n) : 0;
}
size_t Csv::scopeRowLimit(const scope::FrameMetadata &m, bool meter) {
    if (!m.valid()) // Also samples and the rate.
        return 0;
    char text[48];
    const uint64_t last = uint64_t(m.samples - 1);
    size_t bytes = Decimal::integer(last, text, sizeof text);
    // time = index * denominator / numerator: its fraction ends within max(a, b) digits when the
    // numerator over their common factor is 2^a * 5^b, else Decimal::fraction stops at 12.
    uint64_t a = m.samplesPerSecond.numerator, b = m.samplesPerSecond.denominator;
    while (b) { const uint64_t r = a % b; a = b; b = r; }
    uint64_t rest = m.samplesPerSecond.numerator / a;
    unsigned twos = 0, fives = 0;
    while (!(rest % 2)) { rest /= 2; ++twos; }
    while (!(rest % 5)) { rest /= 5; ++fives; }
    const unsigned fraction = rest != 1 ? 12 : twos > fives ? (twos > 12 ? 12 : twos) : (fives > 12 ? 12 : fives);
    const size_t whole = Decimal::integer(last * m.samplesPerSecond.denominator / m.samplesPerSecond.numerator, text, sizeof text);
    if (!bytes || !whole)
        return 0;
    bytes += whole + (fraction ? 1 + fraction : 0);
    for (unsigned channel = 0; channel < 2; ++channel) {
        const auto &c = m.channels[channel];
        if (!c.enabled || (m.layout == scope::Layout::InterleavedCh1 && channel)) continue; // FrameView::code: empty columns.
        size_t volts = 0;
        for (int raw = -128; raw < 128; ++raw) {
            double value;
            if (!c.volts(int16_t(raw), value))
                continue;
            const size_t length = Decimal::number(value, text, sizeof text);
            if (!length)
                return 0; // scope() fails on it as well.
            volts = length > volts ? length : volts;
        }
        bytes += 4 + volts; // A signed 8-bit code: "-128" at most.
    }
    if (meter) bytes += 1 + 13; // Its comma and a float at scope()'s seven digits: "-1.234567e-45".
    return bytes + 5 + 2; // Five commas, CRLF.
}
size_t Csv::scopeMetadata(const scope::FrameMetadata &m, char *output, size_t capacity) {
    if (!output || !capacity || !m.valid())
        return 0;
    char timestamp[24];
    if (!Decimal::integer(m.timestampUs, timestamp, sizeof timestamp))
        return 0;
    int length = std::snprintf(
        output, capacity,
        "# OpenOSC CSV 1\r\n# timestamp_us=%s,sequence=%lu,samples=%lu,ring=%lu,origin=%lu\r\n"
        "# sample_rate=%lu/%lu,timing_verified=%u,layout=%u,triggered=%u,trigger_index=%lu\r\n",
        timestamp, static_cast<unsigned long>(m.sequence), static_cast<unsigned long>(m.samples),
        static_cast<unsigned long>(m.ring), static_cast<unsigned long>(m.origin),
        static_cast<unsigned long>(m.samplesPerSecond.numerator),
        static_cast<unsigned long>(m.samplesPerSecond.denominator), unsigned(m.timingVerified),
        unsigned(m.layout), unsigned(m.triggered), static_cast<unsigned long>(m.triggerIndex));
    if (length <= 0 || size_t(length) >= capacity)
        return 0;
    size_t used = length;
    for (unsigned channel = 0; channel < 2; ++channel) {
        const auto &c = m.channels[channel];
        char scale[40]{}, zero[40]{};
        if (!Decimal::number(c.zeroCode, zero, sizeof zero))
            return 0;
        if (c.calibrated && !Decimal::number(c.voltsPerCode, scale, sizeof scale))
            return 0;
        length = std::snprintf(
            output + used, capacity - used,
            "# ch%u: "
            "enabled=%u,mV_div=%lu,probe=%lu/"
            "%lu,coupling=%s,calibrated=%u,cal_crc=%04X,volts_code=%s,zero_code=%s,analog_position=%d\r\n",
            channel + 1, unsigned(c.enabled), static_cast<unsigned long>(c.millivoltsPerDivision),
            static_cast<unsigned long>(c.probeNumerator), static_cast<unsigned long>(c.probeDenominator),
            c.coupling == scope::Coupling::Dc ? "DC" : "AC", unsigned(c.calibrated),
            unsigned(c.calibrationCrc), scale, zero, int(c.analogPosition));
        if (length <= 0 || size_t(length) >= capacity - used)
            return 0;
        used += length;
    }
    return used;
}
} // namespace openosc
