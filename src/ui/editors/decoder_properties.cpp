#include "openosc/ui/editors/decoder_properties.hpp"
#include "openosc/decode/table_text.hpp"
#include <algorithm>
#include <cstdio>
#include <iterator>

namespace openosc::ui {
using decoding::CanSource;
using decoding::Rs485Source;
using decoding::Protocol;
using decoding::Protocols;
using decoding::TableText;
namespace {
using R = DecoderProperties::Row;
constexpr int Coarse = 1000; // coarseStep() of the rows whose Up/Down step through a series, not by a number.
constexpr uint32_t UartRates[] = {300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 38400, 57600, 76800, 100000, 115200,
                                  128000, 153600, 230400, 250000, 256000, 460800, 500000, 576000, 921600, 1000000, 1500000,
                                  2000000, 2500000, 3000000, 4000000, 5000000, 10000000, 20000000};
constexpr uint32_t LinRates[] = {1200, 2400, 4800, 9600, 10417, 19200, 20000};
constexpr uint32_t CanRates[] = {10000, 20000, 33333, 50000, 62500, 83333, 100000, 125000, 250000, 500000, 800000, 1000000};
constexpr uint32_t CanDataRates[] = {125000, 250000, 500000, 1000000, 2000000, 4000000, 5000000, 8000000, 10000000};
void keepDataRate(decoding::CanSettings &can) { can.dataBitrate = std::max(can.dataBitrate, can.bitrate); }
// A rate: Up/Down to the next standard one beyond it, Left/Right by 1 % (at least 1).
template <size_t N> uint32_t stepRate(uint32_t v, int steps, const uint32_t (&series)[N], uint32_t low, uint32_t high) {
    if (steps >= Coarse) { for (uint32_t r : series) if (r > v) return r; return v; }
    if (steps <= -Coarse) { for (size_t i = N; i-- > 0;) if (series[i] < v) return series[i]; return v; }
    const int64_t next = int64_t(v) + int64_t(steps) * std::max<int64_t>(1, v / 100);
    return uint32_t(std::clamp<int64_t>(next, low, high));
}
// The UART baud: Up/Down to the next standard rate beyond it; Left/Right to the neighbour on one fixed
// scale, the rates of three significant digits with the standard rates in it (9600 -> 9610, 115000 ->
// 115200 -> 116000): from a rate on the scale, Right then Left gives it back. A rate off the scale
// (from the saved settings or Detect baud) is kept until a key moves it, then snaps to its nearest
// scale neighbour on that side, and is not given back.
uint32_t stepBaud(uint32_t v, int steps) {
    if (steps >= Coarse || steps <= -Coarse) return stepRate(v, steps, UartRates, Protocols::MinimumBaud, Protocols::MaximumBaud);
    const auto unit = [](uint32_t x) { uint32_t u = 1; while (x / u >= 1000) u *= 10; return u; }; // Third digit's weight.
    for (; steps > 0 && v < Protocols::MaximumBaud; --steps) {
        uint32_t next = (v / unit(v) + 1) * unit(v);
        for (uint32_t r : UartRates) if (r > v) { next = std::min(next, r); break; }
        v = std::min(next, Protocols::MaximumBaud);
    }
    for (; steps < 0 && v > Protocols::MinimumBaud; ++steps) {
        uint32_t below = ((v - 1) / unit(v - 1)) * unit(v - 1);
        for (size_t i = std::size(UartRates); i-- > 0;) if (UartRates[i] < v) { below = std::max(below, UartRates[i]); break; }
        v = std::max(below, Protocols::MinimumBaud);
    }
    return v;
}
// The SPI pause: Up/Down through 1-2-5 (ns), Left/Right by 1 %.
uint32_t stepPause(uint32_t v, int steps) {
    if (steps >= Coarse || steps <= -Coarse) {
        uint32_t best = v;
        for (uint64_t decade = 1; decade <= Protocols::GapLimitNs; decade *= 10)
            for (uint32_t m : {1u, 2u, 5u}) {
                const uint64_t r = decade * m;
                if (r > Protocols::GapLimitNs) continue;
                if (steps > 0 && r > v && (best == v || r < best)) best = uint32_t(r);
                if (steps < 0 && r < v && (best == v || r > best)) best = uint32_t(r);
            }
        return best;
    }
    const int64_t next = int64_t(v) + int64_t(steps) * std::max<int64_t>(1, v / 100);
    return uint32_t(std::clamp<int64_t>(next, 1, Protocols::GapLimitNs));
}
const char *lineRole(Protocol p, bool first, const decoding::Settings &s) { // The role of channel 1 (first) or 2.
    switch (p) {
    case Protocol::Uart: case Protocol::Rs232: return "RX";
    case Protocol::Lin: return "LIN";
    case Protocol::I2c: return (s.i2c.sdaChannel == 0) == first ? "SDA" : "SCL";
    case Protocol::Spi: return (s.spi.clockChannel == 0) == first ? "SCLK" : "Data";
    case Protocol::Can: return "RXD";
    case Protocol::Rs485: case Protocol::Dmx512: return "RO";
    default: return "";
    }
}
} // namespace
DecoderProperties::DecoderProperties(const decoding::Settings &settings) : settings_(settings) { layout(); }
void DecoderProperties::layout() {
    count_ = 0;
    const auto add = [this](R r) { if (count_ < MaximumRows) rows_[count_++] = r; };
    add(R::Protocol); add(R::View); add(R::Update);
    const auto &s = settings_;
    switch (s.protocol) {
    case Protocol::Uart: case Protocol::Rs232:
        for (R r : {R::Line, R::UartBaud, R::DetectBaud, R::DataBits, R::Parity, R::StopBits, R::BitOrder}) add(r);
        if (s.protocol == Protocol::Uart) add(R::IdleLevel);
        break;
    case Protocol::I2c: add(R::Sda); break;
    case Protocol::Spi:
        for (R r : {R::Sclk, R::SpiMode, R::WordBits, R::SpiOrder, R::WordStart}) add(r);
        add(s.spi.manual ? R::ManualStart : R::PauseNs);
        break;
    case Protocol::Lin: for (R r : {R::Line, R::LinBaud, R::LinChecksum, R::LinId, R::LinLength}) add(r); break;
    case Protocol::Can: for (R r : {R::CanSource, R::CanBitrate, R::SamplePoint, R::DataBitrate, R::DataSamplePoint}) add(r); break;
    case Protocol::Rs485: case Protocol::Dmx512:
        add(R::Rs485Source);
        if (s.rs485.source == Rs485Source::Receiver) add(R::Line);
        if (s.protocol == Protocol::Rs485) for (R r : {R::UartBaud, R::DetectBaud, R::DataBits, R::Parity, R::StopBits, R::BitOrder}) add(r);
        add(R::Polarity);
        break;
    default: break;
    }
    if (s.difference()) add(R::Difference);
    else {
        const unsigned sources = s.sources();
        if (sources & 1) add(R::Threshold1);
        if (sources & 2) add(R::Threshold2);
    }
    add(R::HysteresisMode);
    if (s.hysteresis == decoding::Hysteresis::Manual) add(R::Hysteresis);
    add(R::MinPulse);
    if (s.enabled) add(R::SaveCsv);
}
void DecoderProperties::baudHint(const char *state) { std::snprintf(hint_, sizeof hint_, "%s", state ? state : ""); }
void DecoderProperties::result(const char *summary) { std::snprintf(result_, sizeof result_, "%s", summary ? summary : ""); }
unsigned DecoderProperties::index(Row r) const {
    for (unsigned i = 0; i < count_; ++i) if (rows_[i] == r) return i;
    return count_;
}
const char *DecoderProperties::notice() const {
    if (refused_) return refused_;
    if (!settings_.enabled) return "Decoder off: the Scope as it is";
    if (settings_.protocol == Protocol::Spi) return "SPI without CS: words from pauses or a manual start";
    if (settings_.protocol == Protocol::Can) return "Classical and ISO CAN FD by FDF; data rate after BRS";
    if (settings_.protocol == Protocol::Rs232) return "DC coupling; negative idle, threshold near 0 V";
    if (settings_.protocol == Protocol::Dmx512) return "250 kbaud 8N2; BREAK >= 88 us, MAB >= 8 us";
    if (settings_.protocol == Protocol::Rs485) return "CH1 Data+, CH2 Data-, both DC; idle CH1 > CH2";
    if (settings_.protocol == Protocol::Uart) return "Baud: Up/Down standard rates, Left/Right fine";
    return "Decodes the record on show, whole";
}
PropertyKind DecoderProperties::kind(unsigned i) const {
    switch (row(i)) {
    case R::UartBaud: case R::PauseNs: case R::ManualStart: case R::LinBaud: case R::LinId: case R::CanBitrate: case R::SamplePoint:
    case R::DataBitrate: case R::DataSamplePoint:
    case R::Threshold1: case R::Threshold2: case R::Difference: case R::Hysteresis: case R::MinPulse: return PropertyKind::Number;
    case R::SaveCsv: case R::DetectBaud: return PropertyKind::Action;
    default: return PropertyKind::Choice;
    }
}
void DecoderProperties::name(unsigned i, char *output, size_t capacity) const {
    const auto &s = settings_;
    switch (row(i)) {
    case R::Protocol: std::snprintf(output, capacity, "Protocol"); break;
    case R::View: std::snprintf(output, capacity, "View"); break;
    case R::Update: std::snprintf(output, capacity, "Update"); break;
    case R::Line: std::snprintf(output, capacity, "%s line", s.protocol == Protocol::Lin ? "LIN" : s.rs485Line() ? "RO" : "RX"); break;
    case R::UartBaud: case R::LinBaud: std::snprintf(output, capacity, "Baud"); break;
    case R::DataBits: std::snprintf(output, capacity, "Data bits"); break;
    case R::Parity: std::snprintf(output, capacity, "Parity"); break;
    case R::StopBits: std::snprintf(output, capacity, "Stop bits"); break;
    case R::BitOrder: case R::SpiOrder: std::snprintf(output, capacity, "Bit order"); break;
    case R::IdleLevel: std::snprintf(output, capacity, "Idle level"); break;
    case R::Sda: std::snprintf(output, capacity, "SDA / SCL"); break;
    case R::Sclk: std::snprintf(output, capacity, "SCLK / data"); break;
    case R::SpiMode: std::snprintf(output, capacity, "SPI mode"); break;
    case R::WordBits: std::snprintf(output, capacity, "Word bits"); break;
    case R::WordStart: std::snprintf(output, capacity, "Word start"); break;
    case R::PauseNs: std::snprintf(output, capacity, "Pause"); break;
    case R::ManualStart: std::snprintf(output, capacity, "Start point"); break;
    case R::LinChecksum: std::snprintf(output, capacity, "Checksum"); break;
    case R::LinId: std::snprintf(output, capacity, "Length of ID"); break;
    case R::LinLength: std::snprintf(output, capacity, "ID 0x%02X bytes", unsigned(linId_)); break;
    case R::CanSource: std::snprintf(output, capacity, "Source"); break;
    case R::CanBitrate: std::snprintf(output, capacity, "Bit rate"); break;
    case R::SamplePoint: std::snprintf(output, capacity, "Sample point"); break;
    case R::DataBitrate: std::snprintf(output, capacity, "FD data rate"); break;
    case R::DataSamplePoint: std::snprintf(output, capacity, "FD data SP"); break;
    case R::Threshold1: std::snprintf(output, capacity, "%s thr CH1", lineRole(s.protocol, true, s)); break;
    case R::Threshold2: std::snprintf(output, capacity, "%s thr CH2", lineRole(s.protocol, false, s)); break;
    case R::Difference: std::snprintf(output, capacity, "%s thr", s.protocol == Protocol::Can ? "CANH-CANL" : "CH1-CH2"); break;
    case R::Rs485Source: std::snprintf(output, capacity, "Source"); break;
    case R::Polarity: std::snprintf(output, capacity, "Polarity"); break;
    case R::HysteresisMode: std::snprintf(output, capacity, "Hysteresis"); break;
    case R::Hysteresis: std::snprintf(output, capacity, "Hysteresis mV"); break;
    case R::MinPulse: std::snprintf(output, capacity, "Min pulse"); break;
    case R::SaveCsv: std::snprintf(output, capacity, "Save CSV"); break;
    case R::DetectBaud: std::snprintf(output, capacity, "Detect baud"); break;
    }
}
void DecoderProperties::value(unsigned i, char *output, size_t capacity) const {
    const auto &s = settings_;
    switch (row(i)) {
    case R::UartBaud: std::snprintf(output, capacity, "%lu bd", static_cast<unsigned long>(s.uart.baud)); return;
    case R::LinBaud: std::snprintf(output, capacity, "%lu bd", static_cast<unsigned long>(s.lin.baud)); return;
    case R::CanBitrate: TableText::quantity(s.can.bitrate, "bit/s", output, unsigned(capacity)); return;
    case R::DataBitrate: TableText::quantity(s.can.dataBitrate, "bit/s", output, unsigned(capacity)); return;
    case R::PauseNs: TableText::quantity(s.spi.gapNs * 1e-9, "s", output, unsigned(capacity)); return;
    case R::ManualStart: std::snprintf(output, capacity, "point %lu", static_cast<unsigned long>(s.spi.manualStart)); return;
    case R::LinId: std::snprintf(output, capacity, "0x%02X", unsigned(linId_)); return;
    case R::SamplePoint: case R::DataSamplePoint:
        std::snprintf(output, capacity, "%u.%u %%", unsigned(number(i)) / 10u, unsigned(number(i)) % 10u);
        return;
    case R::Threshold1: case R::Threshold2: case R::Difference: case R::Hysteresis:
        TableText::millivolts(number(i), output, unsigned(capacity));
        return;
    case R::MinPulse:
        if (s.minPulseNs) TableText::quantity(s.minPulseNs * 1e-9, "s", output, unsigned(capacity));
        else std::snprintf(output, capacity, "Off");
        return;
    case R::SaveCsv: std::snprintf(output, capacity, "%s", result_); return;
    case R::DetectBaud: std::snprintf(output, capacity, "%s", hint_); return;
    default: PropertyList::value(i, output, capacity); return;
    }
}
unsigned DecoderProperties::options(unsigned i) const {
    switch (row(i)) {
    case R::Protocol: return unsigned(Protocol::Count);
    case R::View: return unsigned(decoding::View::Count);
    case R::Update: return unsigned(decoding::Update::Count);
    case R::Line: case R::Sda: case R::Sclk: case R::BitOrder: case R::SpiOrder: case R::IdleLevel: case R::WordStart:
    case R::LinChecksum: case R::HysteresisMode: case R::Polarity: return 2;
    case R::Rs485Source: return unsigned(Rs485Source::Count);
    case R::DataBits: case R::Parity: return 5;
    case R::StopBits: return 3;
    case R::SpiMode: return 4;
    case R::WordBits: return 13;
    case R::LinLength: return decoding::LinSettings::MaximumLength + 1;
    case R::CanSource: return unsigned(CanSource::Count);
    default: return 0;
    }
}
unsigned DecoderProperties::option(unsigned i) const {
    const auto &s = settings_;
    switch (row(i)) {
    case R::Protocol: return s.enabled ? unsigned(s.protocol) : 0;
    case R::View: return unsigned(s.view);
    case R::Update: return unsigned(s.update);
    case R::Line: return s.channel;
    case R::DataBits: return s.uart.dataBits - 5u;
    case R::Parity: return s.uart.parity;
    case R::StopBits: return s.uart.stopHalfBits - 2u;
    case R::BitOrder: return s.uart.msbFirst;
    case R::IdleLevel: return s.uart.inverted;
    case R::Sda: return s.i2c.sdaChannel;
    case R::Sclk: return s.spi.clockChannel;
    case R::SpiMode: return s.spi.mode;
    case R::WordBits: return s.spi.bits - 4u;
    case R::SpiOrder: return s.spi.msbFirst;
    case R::WordStart: return s.spi.manual;
    case R::LinChecksum: return s.lin.enhanced;
    case R::LinLength: return s.lin.length(linId_);
    case R::CanSource: return unsigned(s.can.source);
    case R::Rs485Source: return unsigned(s.rs485.source);
    case R::Polarity: return s.rs485.inverted;
    case R::HysteresisMode: return unsigned(s.hysteresis);
    default: return 0;
    }
}
void DecoderProperties::optionName(unsigned i, unsigned index, char *output, size_t capacity) const {
    static constexpr const char *parities[] = {"None", "Even", "Odd", "Mark", "Space"};
    static constexpr const char *stops[] = {"1", "1.5", "2"};
    static constexpr const char *modes[] = {"0 (CPOL 0, CPHA 0)", "1 (CPOL 0, CPHA 1)", "2 (CPOL 1, CPHA 0)", "3 (CPOL 1, CPHA 1)"};
    static constexpr const char *sources[] = {"RXD on CH1", "RXD on CH2", "CANH CH1 - CANL CH2"};
    switch (row(i)) {
    case R::Protocol: std::snprintf(output, capacity, "%s", Protocols::name(Protocol(index))); break;
    case R::View: std::snprintf(output, capacity, "%s", Protocols::viewName(decoding::View(index))); break;
    case R::Update:
        std::snprintf(output, capacity, "%s", index ? (Protocols::liveAvailable() ? "Live" : "Live (unavailable)") : "Complete");
        break;
    case R::Line: std::snprintf(output, capacity, "CH%u", index + 1); break;
    case R::Sda: std::snprintf(output, capacity, "SDA CH%u, SCL CH%u", index + 1, (index ^ 1) + 1); break;
    case R::Sclk: std::snprintf(output, capacity, "SCLK CH%u, data CH%u", index + 1, (index ^ 1) + 1); break;
    case R::DataBits: std::snprintf(output, capacity, "%u", index + 5); break;
    case R::Parity: std::snprintf(output, capacity, "%s", index < 5 ? parities[index] : ""); break;
    case R::StopBits: std::snprintf(output, capacity, "%s", index < 3 ? stops[index] : ""); break;
    case R::BitOrder: case R::SpiOrder: std::snprintf(output, capacity, "%s", index ? "MSB first" : "LSB first"); break;
    case R::IdleLevel: std::snprintf(output, capacity, "%s", index ? "Low (inverted)" : "High"); break;
    case R::SpiMode: std::snprintf(output, capacity, "%s", index < 4 ? modes[index] : ""); break;
    case R::WordBits: std::snprintf(output, capacity, "%u", index + 4); break;
    case R::WordStart: std::snprintf(output, capacity, "%s", index ? "Manual start" : "After a pause"); break;
    case R::LinChecksum: std::snprintf(output, capacity, "%s", index ? "Enhanced" : "Classic"); break;
    case R::LinLength:
        if (index) std::snprintf(output, capacity, "%u", index);
        else std::snprintf(output, capacity, "Unknown");
        break;
    case R::CanSource: std::snprintf(output, capacity, "%s", index < 3 ? sources[index] : ""); break;
    case R::Rs485Source: std::snprintf(output, capacity, "%s", index ? "Receiver RO on CH1/CH2" : "Data+ CH1 - Data- CH2"); break;
    case R::Polarity: std::snprintf(output, capacity, "%s", index ? "Inverted (idle low)" : "Normal (idle high)"); break;
    case R::HysteresisMode:
        if (index) std::snprintf(output, capacity, "Manual (mV)");
        else if (const uint32_t floor = Protocols::autoHysteresisFloorMv(settings_.protocol, settings_.threshold(settings_.channel)))
            std::snprintf(output, capacity, "Auto (%u %%, min %lu.%lu V)", Protocols::AutoHysteresisPercent, // RS-232's floor.
                          static_cast<unsigned long>(floor / 1000), static_cast<unsigned long>(floor % 1000 / 100));
        else std::snprintf(output, capacity, "Auto (%u %% of swing)", Protocols::AutoHysteresisPercent);
        break;
    default: if (capacity) output[0] = 0; break;
    }
}
bool DecoderProperties::choose(unsigned i, unsigned index) {
    refused_ = nullptr;
    if (index >= options(i)) return false;
    auto &s = settings_;
    switch (row(i)) {
    case R::Protocol:
        if (index == 0) s.enabled = 0;
        else { s.protocol = Protocol(index); s.enabled = 1; }
        break;
    case R::View: s.view = decoding::View(index); break;
    case R::Update:
        if (decoding::Update(index) == decoding::Update::Live && !Protocols::liveAvailable()) {
            refused_ = "Live is not available yet: Complete";
            return false;
        }
        s.update = decoding::Update(index);
        break;
    case R::Line: s.channel = index; break;
    case R::DataBits: s.uart.dataBits = uint8_t(index + 5); break;
    case R::Parity: s.uart.parity = uint8_t(index); break;
    case R::StopBits: s.uart.stopHalfBits = uint8_t(index + 2); break;
    case R::BitOrder: s.uart.msbFirst = uint8_t(index); break;
    case R::IdleLevel: s.uart.inverted = uint8_t(index); break;
    case R::Sda: s.i2c.sdaChannel = uint8_t(index); break;
    case R::Sclk: s.spi.clockChannel = uint8_t(index); break;
    case R::SpiMode: s.spi.mode = uint8_t(index); break;
    case R::WordBits: s.spi.bits = uint8_t(index + 4); break;
    case R::SpiOrder: s.spi.msbFirst = uint8_t(index); break;
    case R::WordStart: s.spi.manual = uint8_t(index); break;
    case R::LinChecksum: s.lin.enhanced = uint8_t(index); break;
    case R::LinLength: s.lin.length(linId_, index); break;
    case R::CanSource: s.can.source = CanSource(index); break;
    case R::Rs485Source: s.rs485.source = Rs485Source(index); break;
    case R::Polarity: s.rs485.inverted = uint8_t(index); break;
    case R::HysteresisMode: s.hysteresis = decoding::Hysteresis(index); break;
    default: return false;
    }
    layout(); // The protocol, word start, CAN or RS-485 source or hysteresis mode may change the rows.
    return true;
}
int DecoderProperties::coarseStep(unsigned i) const {
    switch (row(i)) {
    case R::UartBaud: case R::LinBaud: case R::CanBitrate: case R::DataBitrate: case R::PauseNs: case R::ManualStart: return Coarse;
    case R::SamplePoint: case R::DataSamplePoint: return 25;
    case R::LinId: return 8;
    default: return 100; // mV, ns
    }
}
void DecoderProperties::adjust(unsigned i, int steps) {
    auto &s = settings_;
    switch (row(i)) {
    case R::UartBaud: s.uart.baud = stepBaud(s.uart.baud, steps); return;
    case R::LinBaud: s.lin.baud = stepRate(s.lin.baud, steps, LinRates, Protocols::MinimumBaud, Protocols::MaximumBaud); return;
    case R::CanBitrate:
        s.can.bitrate = stepRate(s.can.bitrate, steps, CanRates, Protocols::MinimumBitrate, Protocols::MaximumBitrate);
        keepDataRate(s.can);
        return;
    case R::DataBitrate:
        s.can.dataBitrate = stepRate(s.can.dataBitrate, steps, CanDataRates, s.can.bitrate, Protocols::MaximumDataBitrate);
        keepDataRate(s.can);
        return;
    case R::PauseNs: s.spi.gapNs = stepPause(s.spi.gapNs, steps); return;
    default: setNumber(i, int32_t(std::clamp<int64_t>(int64_t(number(i)) + steps, INT32_MIN, INT32_MAX))); return;
    }
}
int32_t DecoderProperties::number(unsigned i) const {
    const auto &s = settings_;
    switch (row(i)) {
    case R::UartBaud: return int32_t(s.uart.baud);
    case R::LinBaud: return int32_t(s.lin.baud);
    case R::CanBitrate: return int32_t(s.can.bitrate);
    case R::DataBitrate: return int32_t(s.can.dataBitrate);
    case R::PauseNs: return int32_t(s.spi.gapNs);
    case R::ManualStart: return int32_t(std::min<uint32_t>(s.spi.manualStart, INT32_MAX));
    case R::LinId: return linId_;
    case R::SamplePoint: return s.can.samplePointPermille;
    case R::DataSamplePoint: return s.can.dataSamplePointPermille;
    case R::Threshold1: return s.threshold(0);
    case R::Threshold2: return s.threshold(1);
    case R::Difference: return s.differenceThreshold();
    case R::Hysteresis: return int32_t(s.hysteresisMv);
    case R::MinPulse: return int32_t(s.minPulseNs);
    default: return 0;
    }
}
void DecoderProperties::setNumber(unsigned i, int32_t value) {
    auto &s = settings_;
    const auto clamp = [value](int64_t low, int64_t high) { return std::clamp<int64_t>(value, low, high); };
    switch (row(i)) {
    case R::UartBaud: s.uart.baud = uint32_t(clamp(Protocols::MinimumBaud, Protocols::MaximumBaud)); break;
    case R::LinBaud: s.lin.baud = uint32_t(clamp(Protocols::MinimumBaud, Protocols::MaximumBaud)); break;
    case R::CanBitrate: s.can.bitrate = uint32_t(clamp(Protocols::MinimumBitrate, Protocols::MaximumBitrate)); keepDataRate(s.can); break;
    case R::DataBitrate: s.can.dataBitrate = uint32_t(clamp(s.can.bitrate, Protocols::MaximumDataBitrate)); break;
    case R::PauseNs: s.spi.gapNs = uint32_t(clamp(1, Protocols::GapLimitNs)); break;
    case R::ManualStart: s.spi.manualStart = uint32_t(clamp(0, INT32_MAX)); break;
    case R::LinId: linId_ = uint8_t(clamp(0, decoding::LinSettings::Ids - 1)); break;
    case R::SamplePoint: s.can.samplePointPermille = uint16_t(clamp(100, 900)); break;
    case R::DataSamplePoint: s.can.dataSamplePointPermille = uint16_t(clamp(100, 900)); break;
    case R::Threshold1: case R::Threshold2:
        if (s.protocol == Protocol::Rs232)
            s.uart.rs232ThresholdMv = int16_t(clamp(-Protocols::Rs232ThresholdLimitMv, Protocols::Rs232ThresholdLimitMv));
        else s.thresholdMv[row(i) == R::Threshold2] = int32_t(clamp(-Protocols::ThresholdLimitMv, Protocols::ThresholdLimitMv));
        break;
    case R::Difference: s.differenceThreshold(int32_t(clamp(-Protocols::ThresholdLimitMv, Protocols::ThresholdLimitMv))); break;
    case R::Hysteresis: s.hysteresisMv = uint32_t(clamp(0, Protocols::HysteresisLimitMv)); break;
    case R::MinPulse: s.minPulseNs = uint32_t(clamp(0, Protocols::MinPulseLimitNs)); break;
    default: break;
    }
}
} // namespace openosc::ui
