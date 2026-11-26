#include "openosc/decode/table_text.hpp"
#include "openosc/decode/protocols/can_fd_view.hpp"
#include "openosc/decode/protocols/dmx.hpp"
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

// Integers only in the formats: the firmware's nano printf has no %f or %g.
namespace openosc::decoding {
namespace {
using namespace protocols;
constexpr long TimeLimit = 9999999; // "-99999.99": Time characters in hundredths.
constexpr struct { const char *name; int exponent; } Units[] = {{"ns", -9}, {"us", -6}, {"ms", -3}, {"s", 0}};
double scale(int exponent) { return std::pow(10.0, -exponent); }
// Text cut to `width` characters ('~' marks the cut) or padded with spaces to it.
void cell(char *out, const char *text, unsigned width) {
    const size_t length = std::strlen(text);
    if (length > width) { std::memcpy(out, text, width - 1); out[width - 1] = '~'; }
    else { std::memcpy(out, text, length); std::memset(out + length, ' ', width - length); }
}
void hundredths(long value, char *out, unsigned capacity) {
    const long magnitude = value < 0 ? -value : value;
    std::snprintf(out, capacity, "%s%ld.%02ld", value < 0 ? "-" : "", magnitude / 100, magnitude % 100);
}
// Appends to a fixed line; false (nothing added) when the text would pass the line's width.
struct Writer {
    char *out;
    unsigned used = 0, width;
    Writer(char *o, unsigned w) : out(o), width(w) { out[0] = 0; }
    bool add(const char *format, ...) __attribute__((format(printf, 2, 3))) {
        char text[64];
        va_list args;
        va_start(args, format);
        std::vsnprintf(text, sizeof text, format, args);
        va_end(args);
        const size_t n = std::strlen(text);
        if (used + n > width) return false;
        std::memcpy(out + used, text, n + 1);
        used += unsigned(n);
        return true;
    }
};
bool wideCan(const ResultSet &set) { // A 29-bit CAN ID in the set: the wide ID column.
    if (set.protocol() != Protocol::Can) return false;
    for (unsigned i = 0; i < set.count(); ++i)
        if (set.event(i).kind == CanFrame && (set.event(i).field >> 29 & 1)) return true;
    return false;
}
unsigned idWidth(const ResultSet *set) { return set && wideCan(*set) ? 8 : 4; }
// Hex digits of one element: UART and SPI carry their bits in the format meta.
unsigned digits(const DecodeEvent &e) {
    const unsigned kind = protocolOf(e.kind);
    const unsigned bits = (kind == 1 || kind == 3) && formatMeta(e.format) ? formatMeta(e.format) : 8 * elementBytes(e.format);
    return bits ? (bits + 3) / 4 : 2;
}
unsigned element(const ResultSet &set, const DecodeEvent &e, unsigned i) {
    const unsigned size = elementBytes(e.format);
    const uint8_t *p = set.pool(uint16_t(e.payload + i * size));
    return size == 2 ? unsigned(p[0] | p[1] << 8) : p[0];
}
bool payload(const DecodeEvent &e) { return e.payload != 0xFFFF && e.count && elementBytes(e.format); }
// The elements in HEX, space separated, as many as the writer holds (a '~' when cut).
void elements(const ResultSet &set, const DecodeEvent &e, Writer &w) {
    if (!payload(e)) return;
    const unsigned d = digits(e);
    for (unsigned i = 0; i < e.count; ++i) {
        const bool more = i + 1 < e.count;
        if (w.used + d + (more ? 2 : 0) > w.width) { w.add("~"); return; }
        w.add("%0*X%s", int(d), element(set, e, i), more ? " " : "");
    }
}
// An ISO CAN FD frame's mark before its data: FD (no BRS) / FB (BRS), 'e' when ESI is recessive.
const char *fdMark(const CanFdView &v) { return v.brs ? (v.esi ? "FBe" : "FB") : (v.esi ? "FDe" : "FD"); }
void canId(const CanFdView &v, char *id, unsigned idSize) { std::snprintf(id, idSize, v.extended ? "%08X" : "%03X", unsigned(v.id)); }
// ID and DATA of event `index`, by its protocol and kind. CAN frames are read through CanFdView (the one
// reader of Classical and FD frames the table, Detail and the CSV share); one it refuses shows "bad".
void describe(const ResultSet &set, unsigned index, char *id, unsigned idSize, char *data, unsigned dataWidth) {
    const auto &e = set.event(index);
    std::snprintf(id, idSize, "--");
    Writer w(data, dataWidth);
    if (e.flags & DecodeEvent::Continued) w.add("+");
    switch (e.kind) {
    case UartBreak: case DmxBreak: w.add("BREAK"); return;
    case UartFragment: case I2cFragment: case LinFragment: case CanFragment: case DmxFragment: w.add("fragment"); return;
    case DmxPacket: std::snprintf(id, idSize, "%02X", DmxDecoder::startCode(e)); break; // The START code, then the slots.
    case I2cTransaction:
        std::snprintf(id, idSize, (e.aux & 2) ? "%03X%c" : "%02X%c", unsigned(e.field), (e.aux & 1) ? 'R' : 'W');
        break;
    case LinFrame: if (e.field) std::snprintf(id, idSize, "%02X", unsigned(e.field & 0x3F)); break;
    case CanFrame: {
        CanFdView v;
        if (!CanFdView::of(set, index, v)) { w.add("bad frame"); return; }
        canId(v, id, idSize);
        if (v.rtr) { w.add("RTR dlc %u", v.dlc); return; }
        if (v.fd) w.add(e.count ? "%s " : "%s", fdMark(v));
        break;
    }
    case CanErrorFlag: std::snprintf(id, idSize, "ERR"); w.add("error flag %lub", static_cast<unsigned long>(e.aux)); return;
    case CanOverload: std::snprintf(id, idSize, "OVL"); w.add("overload %lub", static_cast<unsigned long>(e.aux)); return;
    default: break;
    }
    elements(set, e, w);
}
const char *kindName(uint8_t kind) {
    switch (kind) {
    case UartData: return "UART data"; case UartBreak: return "UART break"; case UartFragment: return "UART fragment";
    case I2cTransaction: return "I2C transaction"; case I2cFragment: return "I2C fragment";
    case SpiFrame: return "SPI frame"; case LinFrame: return "LIN frame"; case LinFragment: return "LIN fragment";
    case CanFrame: return "CAN frame"; case CanErrorFlag: return "CAN error flag"; case CanOverload: return "CAN overload";
    case CanFragment: return "CAN fragment"; case DmxPacket: return "DMX512 packet"; case DmxBreak: return "DMX512 break";
    case DmxFragment: return "DMX512 fragment"; default: return "event";
    }
}
double seconds(const ResultSet &set, int64_t points) { return double(points) / set.samplesPerSecond.value(); }
// Detail: the lines of an event's own protocol fields (two, CAN frames more), then its thresholds.
unsigned protocolLines(const ResultSet &set, unsigned index) {
    const auto &e = set.event(index);
    if (e.kind != CanFrame) return 2;
    return (e.field & canfd::Fdf) ? 6 : 3;
}
void rateAt(uint32_t bitrate, uint16_t permille, char *out, unsigned capacity) { // "500kbit/s@75.0%"
    char rate[16];
    TableText::quantity(bitrate, "bit/s", rate, sizeof rate);
    std::snprintf(out, capacity, "%s@%u.%u%%", rate, permille / 10u, permille % 10u);
}
const char *stopName(canfd::Stop stop) {
    static constexpr const char *names[] = {"EOF ok", "arbitration", "control", "data", "stuff count", "CRC",
                                            "CRC delimiter", "ACK slot", "ACK delimiter", "EOF", "data clock"};
    static_assert(sizeof names / sizeof *names == unsigned(canfd::Stop::Count), "a name per stop");
    return unsigned(stop) < unsigned(canfd::Stop::Count) ? names[unsigned(stop)] : "?";
}
// A CAN frame's fields, all from CanFdView: format and ID, FD control bits, both rates, the CRC whole
// (CanFdView::hexDigits: 4, 5 or 6 digits), the FD stuff count and where the frame's reception stopped.
void canLine(const ResultSet &set, unsigned index, const Settings &s, unsigned n, Writer &w) {
    CanFdView v;
    if (!CanFdView::of(set, index, v)) { if (!n) w.add("descriptor refused: not shown"); return; }
    char a[24], b[24];
    if (!v.fd) n = n == 0 ? 0 : n == 1 ? 2 : 3; // Classical: ID, rate, CRC.
    switch (n) {
    case 0:
        w.add("%sID 0x%X %s %s DLC ", v.fd ? "FD " : "", unsigned(v.id), v.extended ? "ext" : "std",
              v.fd ? "data" : v.rtr ? "remote" : "data");
        if (v.dlcKnown) w.add("%u = %uB", v.dlc, v.length); else w.add("?");
        return;
    case 1:
        w.add("BRS %u ESI %u RRS %c res %c%s", v.brs, v.esi, v.rrsRecessive ? 'r' : 'd', v.resRecessive ? 'r' : 'd',
              v.dataPhase ? " data phase" : "");
        return;
    case 2:
        rateAt(s.can.bitrate, s.can.samplePointPermille, a, sizeof a);
        if (v.fd && v.brs) { rateAt(s.can.dataBitrate, s.can.dataSamplePointPermille, b, sizeof b); w.add("nom %s data %s", a, b); }
        else w.add("nom %s%s", a, v.fd ? " (no BRS)" : "");
        return;
    case 3:
        if (v.crcBits) {
            const int digits = int(CanFdView::hexDigits(v.crcBits));
            w.add("CRC%u rx %0*lX calc %0*lX", v.crcBits, digits, static_cast<unsigned long>(v.crcReceived), digits,
                  static_cast<unsigned long>(v.crcCalculated));
        } else w.add("CRC not received");
        w.add(" ACK %s", (set.event(index).flags & NoAck) ? "missing" : "ok");
        return;
    case 4:
        if (!v.sbcReceived) { w.add("SBC not received"); return; }
        w.add("SBC %u%u%u P%u = %u own %u %s", v.sbcRaw >> 3 & 1u, v.sbcRaw >> 2 & 1u, v.sbcRaw >> 1 & 1u, v.sbcRaw & 1u,
              v.sbcValue, v.sbcCalculated, !v.sbcParityOk ? "parity" : v.sbcValue != v.sbcCalculated ? "mismatch" : "ok");
        return;
    default:
        w.add("stop %s  CRC del %ub ACK %ub", stopName(v.stop), v.crcDelimiter2 ? 2u : 1u, v.ack2 ? 2u : 1u);
        return;
    }
}
// Detail: the levels this record was decoded with (ResultSet, never the settings now), a line each:
// threshold and the hysteresis used, Auto with its percentage (each line its own swing).
unsigned levelLines(const ResultSet &set) {
    const unsigned n = set.difference ? 1u : unsigned(set.lines & 1) + unsigned(set.lines >> 1 & 1);
    return n ? n : 1;
}
void levelLine(const ResultSet &set, unsigned n, Writer &w) {
    if (!set.levelsKnown) { w.add("levels: not measured"); return; }
    unsigned c = 0;
    if (!set.difference) for (unsigned k = 0; c < 2; ++c) if ((set.lines >> c & 1) && k++ == n) break;
    char a[24], b[24];
    TableText::millivolts(set.thresholdMv[c & 1], a, sizeof a);
    TableText::millivolts(set.hysteresisMv[c & 1], b, sizeof b);
    if (set.difference) w.add("%s thr %s hyst %s", set.protocol() == Protocol::Can ? "CANH-CANL" : "CH1-CH2", a, b);
    else w.add("CH%u thr %s hyst %s", (c & 1) + 1, a, b);
    // RS-232: the band may be its floor (Protocols::autoHysteresisFloorMv), wider than the swing's share.
    const uint32_t floor = Protocols::autoHysteresisFloorMv(set.protocol(), set.thresholdMv[c & 1]);
    if (set.hysteresisAuto) {
        if (floor && set.hysteresisMv[c & 1] == floor) w.add(" auto min");
        else w.add(" auto%u%%", Protocols::AutoHysteresisPercent);
    }
}
// Detail: the result's quality in full (the info line may cap or drop the counts): EVT> (lost
// descriptors + dropped units), OVR, CLIP points, Fs LOW seen, glitches.
void qualityLine(const ResultSet &set, Writer &w) {
    w.add("result");
    const unsigned before = w.used;
    if (set.lost() + set.dropped) w.add(" EVT>%lu", static_cast<unsigned long>(set.lost() + set.dropped));
    if (set.overflows) w.add(" OVR%lu", static_cast<unsigned long>(set.overflows));
    if (set.clipped) w.add(" CLIP%lu", static_cast<unsigned long>(set.clipped));
    if (set.flagsSeen & FsLow) w.add(" FsLOW");
    if (set.glitches) w.add(" glitch%lu", static_cast<unsigned long>(set.glitches));
    if (w.used == before) w.add(" quality ok");
}
void protocolLine(const ResultSet &set, unsigned index, const Settings &s, unsigned n, Writer &w) {
    const auto &e = set.event(index);
    if (e.kind == CanFrame) { canLine(set, index, s, n, w); return; }
    if (n == 0) {
        switch (protocolOf(e.kind)) {
        case 1: {
            static constexpr char parity[] = {'N', 'E', 'O', 'M', 'S'};
            static constexpr const char *stops[] = {"1", "1.5", "2"};
            w.add("%lu bd %u%c%s %s%s", static_cast<unsigned long>(s.uart.baud), s.uart.dataBits, parity[s.uart.parity % 5],
                  stops[(s.uart.stopHalfBits - 2) % 3], s.uart.msbFirst ? "MSB" : "LSB", s.uartInverted() ? " inv" : "");
            if (set.difference) w.add(" CH1-CH2"); else w.add(" %s CH%u", set.protocol() == Protocol::Rs485 ? "RO" : "RX", set.channel + 1u);
            return;
        }
        case 2:
            w.add("addr 0x%0*X %s %s SDA CH%u", (e.aux & 2) ? 3 : 2, unsigned(e.field), (e.aux & 1) ? "read" : "write",
                  (e.aux & 2) ? "10b" : "7b", s.i2c.sdaChannel + 1u);
            return;
        case 3:
            w.add("mode %u %u bit %s  no CS  SCLK CH%u", s.spi.mode, s.spi.bits, s.spi.msbFirst ? "MSB" : "LSB", s.spi.clockChannel + 1u);
            return;
        case 4: w.add("PID 0x%02X ID 0x%02X  CH%u", unsigned(e.field & 0xFF), unsigned(e.field & 0x3F), set.channel + 1u); return;
        case 5: w.add("dominant run %lu bits", static_cast<unsigned long>(e.aux)); return; // Error flag, overload.
        case 6: {
            if (e.kind == DmxFragment) { w.add("before the first BREAK"); return; }
            if (e.kind != DmxPacket) { w.add("no START code  %s", set.difference ? "CH1-CH2" : "RO"); return; }
            const unsigned startCode = DmxDecoder::startCode(e);
            if (!e.count) { w.add("SC %02X 0 slots", startCode); return; }
            const unsigned first = DmxDecoder::firstSlot(set, index);
            w.add("SC %02X slots %u-%u of %u", startCode, first, first + e.count - 1, DmxDecoder::slots(set, index));
            return;
        }
        default: return;
        }
    }
    switch (protocolOf(e.kind)) {
    case 2:
        w.add("addr %s NACKs %u first #%u end %s", (e.aux & 4) ? "NACK" : "ACK", unsigned(e.aux >> 16 & 0xFF),
              unsigned(e.aux >> 8 & 0xFF), (e.aux & 32) ? "STOP" : (e.aux & 16) ? "Sr" : "-");
        return;
    case 3: w.add("partial last word %u bits", unsigned(e.aux & 31)); return;
    case 6: { // ANSI E1.11 table 7: BREAK >= 88 us, MAB >= 8 us at the receiver.
        if (e.kind == DmxFragment) return;
        char a[24] = "?", b[24] = "?";
        if (set.samplesPerSecond.valid()) {
            TableText::quantity(seconds(set, DmxDecoder::breakPoints(e)), "s", a, sizeof a);
            TableText::quantity(seconds(set, DmxDecoder::mabPoints(e)), "s", b, sizeof b);
        }
        w.add("BREAK %s%s MAB %s 250k 8N2", (e.flags & Note) ? ">=" : "", a, b);
        return;
    }
    case 4:
        w.add("chk rx %02X calc %02X %s%s", unsigned(e.aux & 0xFF), unsigned(e.aux >> 8 & 0xFF),
              (e.aux >> 16 & 1) ? "enhanced" : "classic", (e.aux >> 17 & 1) ? " len table" : " len ?");
        return;
    default: return;
    }
}
} // namespace
void TableText::quantity(double value, const char *unit, char *out, unsigned capacity) {
    if (!std::isfinite(value)) { std::snprintf(out, capacity, "?"); return; }
    static constexpr struct { const char *prefix; double factor; } prefixes[] = {
        {"n", 1e-9}, {"u", 1e-6}, {"m", 1e-3}, {"", 1}, {"k", 1e3}, {"M", 1e6}, {"G", 1e9}};
    const double magnitude = std::fabs(value);
    unsigned p = 0;
    while (p + 1 < sizeof prefixes / sizeof *prefixes && magnitude / prefixes[p].factor >= 999.5) ++p;
    const double v = magnitude / prefixes[p].factor;
    const char *sign = value < 0 ? "-" : "";
    // Three significant digits: 850, 12.4, 1.23 (rounded at the precision shown).
    if (v >= 99.95) std::snprintf(out, capacity, "%s%ld%s%s", sign, long(std::lround(v)), prefixes[p].prefix, unit);
    else if (v >= 9.995) {
        const long n = std::lround(v * 10);
        std::snprintf(out, capacity, "%s%ld.%ld%s%s", sign, n / 10, n % 10, prefixes[p].prefix, unit);
    } else {
        const long n = std::lround(v * 100);
        std::snprintf(out, capacity, "%s%ld.%02ld%s%s", sign, n / 100, n % 100, prefixes[p].prefix, unit);
    }
}
void TableText::millivolts(int64_t mv, char *out, unsigned capacity) {
    const uint64_t magnitude = mv < 0 ? uint64_t(-(mv + 1)) + 1 : uint64_t(mv); // Volts below 2^32 for int32/uint32 mV.
    std::snprintf(out, capacity, "%s%lu.%03luV", mv < 0 ? "-" : "", static_cast<unsigned long>(magnitude / 1000),
                  static_cast<unsigned long>(magnitude % 1000));
}
TableText::Unit TableText::unit(const ResultSet &set) {
    Unit unit;
    if (!set.samplesPerSecond.valid()) return unit;
    const double rate = set.samplesPerSecond.value();
    const double before = set.timeOrigin() / rate, after = (double(set.samples) - set.timeOrigin()) / rate;
    const double widest = before > after ? before : std::fabs(after);
    for (const auto &u : Units) {
        unit = {u.name, u.exponent};
        if (widest * scale(u.exponent) * 100 <= TimeLimit) break;
    }
    return unit;
}
void TableText::header(const ResultSet *set, const Unit &unit, Line &out) {
    char time[16];
    std::snprintf(time, sizeof time, "%s,%s", set && !set->triggered ? "t0" : "t", unit.name);
    const unsigned id = idWidth(set), data = Columns - 1 - Time - 1 - id - 1 - 1 - Status;
    std::memset(out, ' ', Columns);
    cell(out + 1, time, Time);
    cell(out + 2 + Time, "ID", id);
    cell(out + 3 + Time + id, "DATA", data);
    cell(out + 4 + Time + id + data, "ST", Status);
    out[Columns] = 0;
}
const char *TableText::code(const DecodeEvent &e) {
    const unsigned protocol = protocolOf(e.kind);
    if (e.flags & StuffError) return "STF";
    if (e.flags & CheckFailed) return protocol == 5 ? "CRC" : protocol == 6 ? "LEN" : "CHK";
    if (e.flags & ParityError) return "PE";
    if (e.flags & Framing) return protocol == 1 || protocol == 4 || protocol == 6 ? "FE" : "FRM";
    if (e.flags & SyncError) return protocol == 6 ? "TIM" : "SYN";
    if (e.flags & NoAck) return protocol == 5 ? "ACK" : "NAK";
    if (e.kind == UartBreak || e.kind == DmxBreak) return "BRK";
    if (e.flags & Truncated) return "EVT>";
    if (e.flags & DecodeEvent::Cut) return "CUT";
    if (e.flags & LengthUnknown) return "LEN?";
    if (e.flags & Ambiguous) return "?";
    if (e.flags & NotChecked) return "N/C";
    if (e.flags & FsLow) return "Fs?";
    if (e.flags & DecodeEvent::Continued) return "CONT";
    return "ok";
}
const char *TableText::code(const ResultSet &set, unsigned index) {
    const auto &e = set.event(index);
    const char *c = code(e);
    CanFdView v; // An FD frame whose CRC matched but whose stuff count did not: SBC, not CRC.
    if (std::strcmp(c, "CRC") || e.kind != CanFrame || !(e.field & canfd::Fdf) || !CanFdView::of(set, index, v)) return c;
    return v.crcBits && v.crcReceived == v.crcCalculated && (e.aux & (canfd::SbcParity | canfd::SbcMismatch)) ? "SBC" : c;
}
bool TableText::error(const DecodeEvent &e) {
    return (e.flags & (ErrorFlags | Truncated | DecodeEvent::Cut)) || e.kind == UartBreak || e.kind == DmxBreak;
}
void TableText::label(const ResultSet &set, unsigned index, char *out, unsigned capacity) {
    const auto &e = set.event(index);
    if (error(e)) { std::snprintf(out, capacity, "%s", code(set, index)); return; }
    char id[16], data[16];
    describe(set, index, id, sizeof id, data, 7);
    const bool hasId = std::strcmp(id, "--") != 0;
    CanFdView v;
    if (e.kind == CanFrame && (e.field & canfd::Fdf) && CanFdView::of(set, index, v)) {
        if (v.dlcKnown) std::snprintf(out, capacity, "%s %s %uB", id, fdMark(v), v.length);
        else std::snprintf(out, capacity, "%s %s ?B", id, fdMark(v));
    } else if (e.kind == LinFrame && hasId) std::snprintf(out, capacity, "ID%s", id);
    else if (e.kind == DmxPacket) std::snprintf(out, capacity, "SC%s %u", id, DmxDecoder::slots(set, index));
    else if (hasId) std::snprintf(out, capacity, "%s", id);
    else { // The first element, or the whole data word ("fragment" fits as "fragme~": shown as "..").
        if (char *space = std::strchr(data, ' ')) *space = 0;
        std::snprintf(out, capacity, "%s", e.kind == UartFragment || e.kind == I2cFragment || e.kind == LinFragment ||
                                               e.kind == CanFragment || e.kind == DmxFragment ? ".." : data);
    }
}
void TableText::row(const ResultSet &set, unsigned index, const Unit &unit, bool selected, Line &out) {
    std::memset(out, ' ', Columns);
    out[Columns] = 0;
    if (index >= set.count()) return;
    const auto &e = set.event(index);
    out[0] = selected ? '>' : ' ';
    const unsigned idW = idWidth(&set), dataW = Columns - 1 - Time - 1 - idW - 1 - 1 - Status;
    char time[32] = "?", id[16], data[48];
    if (set.samplesPerSecond.valid()) {
        const double value = seconds(set, int64_t(e.first) - int64_t(set.timeOrigin())) * scale(unit.exponent) * 100;
        if (std::fabs(value) <= TimeLimit) hundredths(long(std::lround(value)), time, sizeof time);
    }
    describe(set, index, id, sizeof id, data, dataW);
    char right[40];
    std::snprintf(right, sizeof right, "%9s", time); // Right-aligned: the decimal points line up.
    cell(out + 1, right, Time);
    cell(out + 2 + Time, id, idW);
    cell(out + 3 + Time + idW, data, dataW);
    cell(out + 4 + Time + idW + dataW, code(set, index), Status);
}
bool TableText::detail(const ResultSet &set, unsigned index, const Settings &s, unsigned line, Line &out) {
    out[0] = 0;
    if (index >= set.count()) return false;
    const auto &e = set.event(index);
    Writer w(out, Capacity - 1);
    const bool rate = set.samplesPerSecond.valid();
    char a[24] = "?", b[24] = "?";
    switch (line) {
    case 0: w.add("#%u of %u  %s", index + 1, set.count(), kindName(e.kind)); return true;
    case 1:
        if (rate) {
            quantity(seconds(set, int64_t(e.first) - int64_t(set.timeOrigin())), "s", a, sizeof a);
            quantity(seconds(set, int64_t(e.last) - int64_t(e.first)), "s", b, sizeof b);
        }
        w.add("t %s from %s  len %s", a, set.triggered ? "trigger" : "t0 start", b);
        return true;
    case 2:
        if (rate) quantity(set.samplesPerSecond.value(), "S/s", a, sizeof a);
        w.add("points %lu..%lu Fs %s", static_cast<unsigned long>(e.first), static_cast<unsigned long>(e.last), a);
        return true;
    case 3: // The edges' uncertainty (plan §6.2): one point, plus the lanes' read skew in four-lane layouts.
        if (rate) quantity(seconds(set, set.window), "s", a, sizeof a);
        w.add("edge time +-%lu pt (%s)", static_cast<unsigned long>(set.window), a);
        return true;
    case 4: {
        w.add("flags:");
        static constexpr struct { uint16_t flag; const char *name; } names[] = {
            {DecodeEvent::Cut, "CUT"}, {DecodeEvent::Open, "OPEN"}, {DecodeEvent::Continued, "CONT"}, {DecodeEvent::Glitch, "GLITCH"},
            {Ambiguous, "?"}, {FsLow, "FsLOW"}, {Truncated, "EVT>"}, {Framing, "FRAMING"}, {ParityError, "PARITY"},
            {CheckFailed, "CHECK"}, {NotChecked, "NOTCHECKED"}, {NoAck, "NOACK"}, {StuffError, "STUFF"},
            {SyncError, "SYNC"}, {LengthUnknown, "LEN?"}, {Note, "NOTE"}};
        bool any = false;
        for (const auto &n : names)
            if (e.flags & n.flag) { if (!w.add(" %s", n.name)) w.add("~"); any = true; }
        if (!any) w.add(" none");
        return true;
    }
    default: break;
    }
    const unsigned own = protocolLines(set, index), levels = levelLines(set);
    if (line < 5 + own) { protocolLine(set, index, s, line - 5, w); return true; }
    if (line < 5 + own + levels) { levelLine(set, line - 5 - own, w); return true; }
    if (line == 5 + own + levels) { qualityLine(set, w); return true; }
    const unsigned first = 6 + own + levels; // The first data line.
    // Then every element: HEX, eight a line with their ASCII (bytes), six a line (16-bit words).
    if (!payload(e)) {
        if (line != first) return false;
        w.add("no data");
        return true;
    }
    const bool words = elementBytes(e.format) == 2;
    const unsigned per = words ? 6 : 8, from = (line - first) * per;
    if (from >= e.count) return false;
    w.add("%03u", from + (e.kind == DmxPacket ? DmxDecoder::firstSlot(set, index) : 0u)); // DMX512: the slot number.
    for (unsigned i = from; i < from + per && i < e.count; ++i) w.add(words ? " %04X" : " %02X", element(set, e, i));
    if (!words) {
        w.add("  ");
        for (unsigned i = from; i < from + per && i < e.count; ++i) {
            const unsigned c = element(set, e, i);
            w.add("%c", c >= 32 && c < 127 ? char(c) : '.');
        }
    }
    return true;
}
void TableText::info(const Settings &s, const ResultSet *shown, const ResultSet *building, const Counters &c, Line &out) {
    // Words in their place in the line, each a priority: Must (the state, EXPORT, cov and every quality
    // word) always; Rate, Events, Skip, Age only while they fit InfoWidth. Short of room, step by step:
    // the quality counts capped (99+), then left out, then every Must word in its short form.
    enum : uint8_t { Must = 0, Rate = 1, Events = 2, Skip = 3, Age = 4 };
    struct Word { const char *name, *brief; uint32_t count; uint8_t priority; char text[24]; bool counted, percent; };
    Word words[14];
    unsigned n = 0;
    const auto word = [&](uint8_t priority, const char *name, const char *brief, uint32_t count = 0, bool counted = false, bool percent = false) {
        words[n++] = {name, brief, count, priority, {}, counted, percent};
    };
    const bool done = shown && shown->status == DecodeStatus::Done;
    const auto percentOf = [](const ResultSet &r) { return uint32_t(uint64_t(r.processed) * 100 / r.samples); };
    if (building && building->samples)
        word(Must, building->measuring ? "LEVELS " : "DEC ", building->measuring ? "L" : "D", percentOf(*building), true, true);
    else if (!shown) word(Must, "WAIT", "WAIT");
    else if (shown->status != DecodeStatus::Done) // Done: said by cov 100% (a result published whole).
        word(Must, statusWord(shown->status, false), statusWord(shown->status, true));
    if (c.exporting) word(Must, "EXPORT", "EXP");
    if (shown && shown->samples) word(Must, "cov", "cov", percentOf(*shown), true, true);
    const unsigned qualityFirst = n;
    if (s.protocol == Protocol::Spi) word(Must, "noCS", "nCS");
    if (done && shown->lost() + shown->dropped) word(Must, "EVT>", "E>", shown->lost() + shown->dropped, true);
    if (done && shown->overflows) word(Must, "OVR", "OV", shown->overflows, true);
    if (done && shown->clipped) word(Must, "CLIP", "CL");
    if (done && (shown->flagsSeen & FsLow)) word(Must, "FsLOW", "FsL");
    if (done && shown->glitches) word(Must, "GL", "GL", shown->glitches, true);
    const unsigned qualityEnd = n;
    if (c.recordsPerSecondX10) word(Rate, "r/s", "r/s", c.recordsPerSecondX10, true);
    if (done) word(Events, "ev", "ev", shown->count(), true);
    if (c.skipped) word(Skip, "skip", "skip", c.skipped, true);
    if (c.age) word(Age, "age", "age", c.ageMs / 1000, true); // Whole seconds: redrawn once a second.
    const auto render = [&](unsigned level) { // 0 as is, 1 quality counts capped, 2 without them, 3 brief.
        for (unsigned i = 0; i < n; ++i) {
            auto &w = words[i];
            const bool quality = i >= qualityFirst && i < qualityEnd;
            const char *name = level >= 3 && w.priority == Must ? w.brief : w.name;
            const unsigned long k = w.count;
            if (!w.counted || (quality && level >= 2)) std::snprintf(w.text, sizeof w.text, "%s", name);
            else if (w.percent) std::snprintf(w.text, sizeof w.text, "%s%lu%%", name, k);
            else if (quality && level == 1 && k > 99) std::snprintf(w.text, sizeof w.text, "%s99+", name);
            else if (w.name[0] == 'r') std::snprintf(w.text, sizeof w.text, "%lu.%lur/s", k / 10, k % 10);
            else if (w.name[0] == 'e' || w.name[0] == 'a') std::snprintf(w.text, sizeof w.text, w.name[0] == 'e' ? "%luev" : "age%lus", k);
            else std::snprintf(w.text, sizeof w.text, "%s%lu", name, k);
        }
    };
    const auto width = [&](uint8_t upTo) {
        unsigned total = 0, any = 0;
        for (unsigned i = 0; i < n; ++i) if (words[i].priority <= upTo) total += unsigned(std::strlen(words[i].text)) + (any++ ? 1 : 0);
        return total;
    };
    unsigned level = 0;
    for (render(level); level < 3 && width(Must) > InfoWidth;) render(++level);
    uint8_t keep = Must;
    while (keep < Age && width(uint8_t(keep + 1)) <= InfoWidth) ++keep;
    Writer w(out, InfoWidth);
    for (unsigned i = 0; i < n; ++i)
        if (words[i].priority <= keep && !w.add(w.used ? " %s" : "%s", words[i].text)) { // Never silently: the cut says so.
            if (w.used < InfoWidth) w.add("~"); else out[InfoWidth - 1] = '~';
            break;
        }
}
const char *TableText::statusWord(DecodeStatus status, bool brief) {
    switch (status) {
    case DecodeStatus::Done: return "";
    case DecodeStatus::NoScale: return "NO SCALE";
    case DecodeStatus::OutOfRange: return "THR OUT";
    case DecodeStatus::ChannelOff: return brief ? "SRC OFF" : "SOURCE OFF";
    case DecodeStatus::Timing: return "RATE OUT";
    case DecodeStatus::Coupling: return brief ? "AC CPL" : "AC: USE DC";
    default: return "WAIT";
    }
}
const char *TableText::empty(const ResultSet *shown, bool decoding) {
    if (!shown) return decoding ? "Decoding the record..." : "Waiting for a record";
    switch (shown->status) {
    case DecodeStatus::Done: return "No events in this record";
    case DecodeStatus::NoScale: return "Source not calibrated: no threshold";
    case DecodeStatus::OutOfRange: return "Threshold outside the source's range";
    case DecodeStatus::ChannelOff: return "A source channel is off or not recorded";
    case DecodeStatus::Timing: return "Bit time out of range at this Fs";
    case DecodeStatus::Unsupported: return "Not decoded by this firmware";
    case DecodeStatus::Coupling: return "Source AC coupled: set DC on CH1 and CH2";
    default: return "Waiting for a record";
    }
}
} // namespace openosc::decoding
