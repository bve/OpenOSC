#include "openosc/decode/settings.hpp"
#include <cstring>
#include <initializer_list>

namespace openosc::decoding {
namespace {
uint32_t mix(uint32_t hash, uint32_t value) { // FNV-1a over the value's four bytes.
    for (unsigned i = 0; i < 4; ++i) { hash ^= (value >> (8 * i)) & 0xFF; hash *= 16777619u; }
    return hash;
}
// Plain bounds (std::abs(INT32_MIN) would overflow).
bool within(int32_t millivolts, int32_t limit = Protocols::ThresholdLimitMv) { return millivolts >= -limit && millivolts <= limit; }
bool rate(uint32_t baud) { return baud >= Protocols::MinimumBaud && baud <= Protocols::MaximumBaud; }
} // namespace
bool LinSettings::operator==(const LinSettings &o) const {
    return baud == o.baud && enhanced == o.enhanced && !std::memcmp(lengths, o.lengths, sizeof lengths);
}
bool Settings::valid() const {
    bool lengths = true;
    for (unsigned id = 0; id < LinSettings::Ids; ++id) lengths &= lin.length(id) <= LinSettings::MaximumLength;
    return enabled <= 1 && unsigned(protocol) < unsigned(Protocol::Count) && !(enabled && protocol == Protocol::Off) && unsigned(view) < unsigned(View::Count) &&
           unsigned(update) < unsigned(Update::Count) && unsigned(hysteresis) < unsigned(Hysteresis::Count) && channel <= 1 && within(thresholdMv[0]) && within(thresholdMv[1]) &&
           hysteresisMv <= Protocols::HysteresisLimitMv && minPulseNs <= Protocols::MinPulseLimitNs &&
           rate(uart.baud) && uart.dataBits >= 5 && uart.dataBits <= 9 && uart.parity <= 4 && uart.stopHalfBits >= 2 &&
           uart.stopHalfBits <= 4 && uart.msbFirst <= 1 && uart.inverted <= 1 &&
           within(uart.rs232ThresholdMv, Protocols::Rs232ThresholdLimitMv) && i2c.sdaChannel <= 1 &&
           spi.clockChannel <= 1 && spi.mode <= 3 && spi.bits >= 4 && spi.bits <= 16 && spi.msbFirst <= 1 && spi.manual <= 1 &&
           (spi.manual || spi.gapNs) && spi.gapNs <= Protocols::GapLimitNs && rate(lin.baud) && lin.enhanced <= 1 && lengths &&
           can.bitrate >= Protocols::MinimumBitrate && can.bitrate <= Protocols::MaximumBitrate &&
           can.samplePointPermille >= 100 && can.samplePointPermille <= 900 && unsigned(can.source) < unsigned(CanSource::Count) &&
           within(can.differenceMv) && can.dataBitrate >= can.bitrate && can.dataBitrate <= Protocols::MaximumDataBitrate &&
           can.dataSamplePointPermille >= 100 && can.dataSamplePointPermille <= 900;
}
uint32_t Settings::hash() const {
    uint32_t h = 2166136261u;
    for (uint32_t v : {uint32_t(protocol), uint32_t(update), uint32_t(hysteresis), channel, uint32_t(thresholdMv[0]), uint32_t(thresholdMv[1]),
                       hysteresisMv, minPulseNs, uart.baud, uint32_t(uart.dataBits | uart.parity << 8 | uart.stopHalfBits << 16),
                       uint32_t(uart.msbFirst | uart.inverted << 8 | i2c.sdaChannel << 16),
                       uint32_t(spi.clockChannel | spi.mode << 8 | spi.bits << 16 | uint32_t(spi.msbFirst) << 24), uint32_t(spi.manual),
                       spi.gapNs, spi.manualStart, lin.baud, uint32_t(lin.enhanced), can.bitrate, uint32_t(can.samplePointPermille),
                       uint32_t(can.source), uint32_t(can.differenceMv), can.dataBitrate, uint32_t(can.dataSamplePointPermille)})
        h = mix(h, v);
    for (uint8_t b : lin.lengths) h = mix(h, b);
    if (protocol == Protocol::Rs232) h = mix(h, uint32_t(uart.rs232ThresholdMv));
    return h;
}
unsigned Settings::sources() const {
    switch (protocol) {
    case Protocol::Uart: case Protocol::Rs232: case Protocol::Lin: return 1u << channel;
    case Protocol::I2c: case Protocol::Spi: return 3;
    case Protocol::Can: return can.source == CanSource::Difference ? 3u : can.source == CanSource::RxdCh2 ? 2u : 1u;
    default: return 0;
    }
}
bool Settings::operator==(const Settings &o) const {
    return enabled == o.enabled && protocol == o.protocol && view == o.view && update == o.update && channel == o.channel &&
           thresholdMv[0] == o.thresholdMv[0] && thresholdMv[1] == o.thresholdMv[1] && hysteresis == o.hysteresis && hysteresisMv == o.hysteresisMv &&
           minPulseNs == o.minPulseNs && uart == o.uart && i2c == o.i2c && spi == o.spi && lin == o.lin && can == o.can;
}
const char *Protocols::name(Protocol p) {
    static constexpr const char *names[] = {"Off", "UART (TTL)", "I2C", "SPI", "LIN", "CAN", "RS-232"};
    static_assert(sizeof names / sizeof *names == unsigned(Protocol::Count), "a name per protocol");
    return unsigned(p) < unsigned(Protocol::Count) ? names[unsigned(p)] : "?";
}
const char *Protocols::key(Protocol p) {
    static constexpr const char *keys[] = {"off", "uart", "i2c", "spi", "lin", "can", "rs232"};
    static_assert(sizeof keys / sizeof *keys == unsigned(Protocol::Count), "a key per protocol");
    return unsigned(p) < unsigned(Protocol::Count) ? keys[unsigned(p)] : "";
}
const char *Protocols::viewName(View v) {
    static constexpr const char *names[] = {"Wave + annotations", "Split", "Decoder only"};
    return unsigned(v) < unsigned(View::Count) ? names[unsigned(v)] : "?";
}
const char *Protocols::viewKey(View v) {
    static constexpr const char *keys[] = {"annotations", "split", "table"};
    return unsigned(v) < unsigned(View::Count) ? keys[unsigned(v)] : "";
}
const char *Protocols::updateKey(Update u) { return u == Update::Live ? "live" : "complete"; }
const char *Protocols::hysteresisKey(Hysteresis h) { return h == Hysteresis::Manual ? "manual" : "auto"; }
} // namespace openosc::decoding
