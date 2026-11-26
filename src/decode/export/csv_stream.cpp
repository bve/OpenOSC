#include "openosc/decode/export/csv_stream.hpp"
#include "openosc/data/decimal.hpp"
#include "openosc/decode/protocols/can_fd_view.hpp"
#include "openosc/decode/protocols/types.hpp"
#include "openosc/decode/table_text.hpp"
#include <cstring>

// Integers and Decimal only: the firmware's nano printf has no %f or %g, and no printf is needed.
namespace openosc::decoding::exporting {
namespace {
using namespace protocols;
constexpr const char *Names[DecodeCsvStream::Columns] = {
    "event_index", "group", "cont", "group_last", "start_sample", "end_sample", "start_s", "end_s", "time_ref",
    "protocol", "kind", "status", "flags", "flags_hex", "id_hex", "id_bits", "lin_pid_hex", "rw", "element_bits",
    "element_count", "data_hex", "data_ascii", "crc_model", "crc_state", "crc_received", "crc_calculated", "ack",
    "nak_count", "nak_first", "start_cond", "end_cond", "can_extended", "can_rtr", "can_dlc", "spi_partial_bits",
    "dominant_bits", "aux_hex",
    // v2: CAN / ISO CAN FD (docs/SCOPE_PROTOCOL_DECODERS_CSV.md §3.5), after every v1 column.
    "can_fd", "can_brs", "can_esi", "can_rrs", "can_res", "crc_bits", "sbc_received", "sbc_value", "sbc_calculated",
    "sbc_parity", "stop_field", "crc_delimiter_bits", "ack_bits"};
enum Column : uint8_t {
    EventIndex, Group, Cont, GroupLast, StartSample, EndSample, StartS, EndS, TimeRef, ProtocolName, KindName, Status,
    FlagNames, FlagsHex, IdHex, IdBits, LinPid, ReadWrite, ElementBits, ElementCount, DataHex, DataAscii, CrcModel,
    CrcState, CrcReceived, CrcCalculated, Ack, NakCount, NakFirst, StartCond, EndCond, CanExtended, CanRtr, CanDlc,
    SpiPartial, DominantBits, AuxHex, CanFd, CanBrs, CanEsi, CanRrs, CanRes, CrcBits, SbcReceivedBits, SbcValue,
    SbcCalculated, SbcParityState, StopField, CrcDelimiterBits, AckBits};
constexpr const char *StopNames[] = {"complete", "arbitration", "control", "data", "stuff_count", "crc", "crc_delimiter",
                                     "ack_slot", "ack_delimiter", "eof", "data_clock"};
static_assert(sizeof StopNames / sizeof *StopNames == unsigned(canfd::Stop::Count), "a name for every stop");
// Flag names, bit by bit (types.hpp, DecodeEvent::Flags).
constexpr const char *FlagName[16] = {"OPEN", "CUT", "CONT", "GLITCH", "AMBIGUOUS", "FS_LOW", "TRUNCATED", "FRAMING",
                                      "PARITY", "CHECK_FAILED", "NOT_CHECKED", "NO_ACK", "STUFF", "SYNC",
                                      "LEN_UNKNOWN", "NOTE"};
constexpr const char *StatusNames[] = {"idle", "decoding", "done", "cancelled", "no_scale", "out_of_range",
                                       "channel_off", "unsupported", "timing"};
// The I2C summary in aux (i2c.cpp) and the LIN checksum model bits (lin.cpp), as documented in
// docs/SCOPE_PROTOCOL_DECODERS_PROTOCOLS.md §4.3.
constexpr uint32_t I2cRead = 1u << 0, I2cTenBit = 1u << 1, I2cAddressNack = 1u << 2, I2cBeganSr = 1u << 3,
                   I2cEndedSr = 1u << 4, I2cEndedStop = 1u << 5;
constexpr uint32_t LinEnhanced = 1u << 16, LinLengthKnown = 1u << 17;

const char *kindName(uint8_t kind) {
    switch (kind) {
    case UartData: return "uart_data";
    case UartBreak: return "uart_break";
    case UartFragment: return "uart_fragment";
    case I2cTransaction: return "i2c_transaction";
    case I2cFragment: return "i2c_fragment";
    case SpiFrame: return "spi_frame";
    case LinFrame: return "lin_frame";
    case LinFragment: return "lin_fragment";
    case CanFrame: return "can_frame";
    case CanErrorFlag: return "can_error_flag";
    case CanOverload: return "can_overload";
    case CanFragment: return "can_fragment";
    default: return nullptr;
    }
}
bool grouped(const DecodeEvent &e) { return protocolOf(e.kind) >= 1 && protocolOf(e.kind) <= 4; }
// The PID was received: the decoder then marks the length as known (aux) or unknown (flag).
bool linPid(const DecodeEvent &e) { return e.kind == LinFrame && ((e.aux & LinLengthKnown) || (e.flags & LengthUnknown)); }
// The checksum byte was received and compared (lin.cpp: aux bits 0-15 are written only then). A frame
// closed earlier does not always carry NotChecked, so every way to close it early is excluded here.
bool linVerified(const DecodeEvent &e) {
    return e.kind == LinFrame && (e.aux & LinLengthKnown) &&
           !(e.flags & (NotChecked | SyncError | DecodeEvent::Cut | DecodeEvent::Open));
}
// A CAN frame whose ACK slot was seen dominant. CanDecoder emits a frame on four paths: EOF 7 reached
// (flags 0 of its own: the ACK slot is long sampled), a stuff error (StuffError), a form error in the
// CRC/ACK delimiter or EOF (Framing) and the record's end (Cut); NoAck is set only once the slot was
// sampled recessive. So a frame carrying nothing but what the completed path adds (CheckFailed, Note,
// FsLow) or the storage adds to a whole unit (Truncated without Cut) passed the slot with it dominant.
// Every other frame may have ended before the slot (or a glitch may have hidden it): unknown.
constexpr uint16_t CanCompleteFlags = CheckFailed | Note | FsLow | Truncated;
bool canAcked(const DecodeEvent &e) { return e.kind == CanFrame && !(e.flags & NoAck) && !(e.flags & ~CanCompleteFlags); }
uint16_t element(const ResultSet &set, const DecodeEvent &e, unsigned i) {
    const uint8_t *p = set.pool(e.payload);
    return elementBytes(e.format) == 2 ? uint16_t(p[2 * i] | (p[2 * i + 1] << 8)) : p[i];
}
// A descriptor the table can read without going outside the pool or guessing its protocol.
bool readable(const ResultSet &set, unsigned index, Protocol protocol) {
    const DecodeEvent &e = set.event(index);
    CanFdView view;
    if (e.kind == CanFrame && !CanFdView::of(set, index, view)) return false; // Count, trailer, fields agree.
    const auto family = protocol == Protocol::Rs232 ? Protocol::Uart : protocol; // Both profiles emit UART events.
    if (!kindName(e.kind) || protocolOf(e.kind) != unsigned(family) || e.first > e.last) return false;
    const unsigned width = formatWidth(e.format);
    if (width > DecodeEvent::Words16) return false;
    if (!e.count) return true;
    if (!width || e.payload == 0xFFFF) return false;
    return unsigned(e.payload) + unsigned(e.count) * elementBytes(e.format) <= ResultSet::Pool;
}
bool copy(const char *text, char *out, size_t capacity) {
    out[0] = 0;
    if (!text) return true;
    const size_t n = std::strlen(text);
    if (n >= capacity) return false;
    std::memcpy(out, text, n + 1);
    return true;
}
} // namespace

const char *DecodeCsvStream::column(unsigned index) { return index < Columns ? Names[index] : ""; }

bool DecodeCsvStream::begin(const ResultSet &set, const CsvContext &context) {
    end();
    error_ = Error::None;
    errorEvent_ = 0;
    if (set.protocol() == Protocol::Off || unsigned(set.protocol()) >= unsigned(Protocol::Count)) error_ = Error::Protocol;
    else if (!set.key().valid) error_ = Error::Record;
    else if (set.status == DecodeStatus::Idle || set.status == DecodeStatus::Decoding ||
             unsigned(set.status) >= sizeof StatusNames / sizeof *StatusNames) error_ = Error::Unfinished;
    else if (set.count() > ResultSet::Events) error_ = Error::Events;
    else if (context.lineCount > CsvContext::MaxLines || !copy(context.firmware, firmware_, sizeof firmware_))
        error_ = Error::Context;
    for (unsigned i = 0; error_ == Error::None && i < context.lineCount; ++i) {
        const auto &l = context.lines[i];
        if (!copy(l.role, lines_[i].role, sizeof lines_[i].role)) error_ = Error::Context;
        lines_[i].channel = l.channel;
        lines_[i].threshold = l.threshold;
        lines_[i].thresholdMv = l.thresholdMv;
        lines_[i].hysteresisMv = l.hysteresisMv;
        lines_[i].hysteresisAuto = l.hysteresisAuto;
    }
    for (unsigned i = 0; error_ == Error::None && i < set.count(); ++i)
        if (!readable(set, i, set.protocol())) { error_ = Error::Descriptor; errorEvent_ = i; }
    if (error_ != Error::None) return false;
    set_ = &set;
    lineCount_ = context.lineCount;
    trigger_ = context.reference == CsvContext::TimeReference::Trigger;
    origin_ = trigger_ ? context.triggerIndex : 0;
    section_ = Section::Metadata;
    item_ = element_ = event_ = 0;
    column_ = 0;
    chunkLen_ = chunkPos_ = 0;
    written_ = 0;
    return true;
}
void DecodeCsvStream::end() {
    set_ = nullptr;
    section_ = Section::Done;
    chunkLen_ = chunkPos_ = 0;
}
size_t DecodeCsvStream::read(uint8_t *output, size_t capacity) {
    if (!set_ || !output) return 0;
    size_t n = 0;
    while (n < capacity) {
        if (chunkPos_ == chunkLen_) {
            if (section_ == Section::Done) break;
            fill();
            continue;
        }
        size_t take = size_t(chunkLen_ - chunkPos_);
        if (take > capacity - n) take = capacity - n;
        std::memcpy(output + n, chunk_ + chunkPos_, take);
        chunkPos_ = uint8_t(chunkPos_ + take);
        n += take;
    }
    written_ += n;
    return n;
}
void DecodeCsvStream::fill() {
    chunkLen_ = chunkPos_ = 0;
    // Each pass writes one piece (a metadata line or character, a column, an element, a flag name);
    // a piece may be empty (a flag not set), so loop until a byte is there or the file ends.
    while (!chunkLen_ && section_ != Section::Done) {
        switch (section_) {
        case Section::Metadata:
            if (!metadata()) { section_ = Section::Header; item_ = 0; }
            break;
        case Section::Header:
            if (item_) put(',');
            put(Names[item_]);
            if (++item_ == Columns) {
                put("\r\n");
                section_ = set_->count() ? Section::Rows : Section::Done;
                event_ = 0; column_ = 0; element_ = 0;
            }
            break;
        case Section::Rows:
            if (cell() && ++event_ == set_->count()) section_ = Section::Done;
            break;
        case Section::Done: break;
        }
    }
}
void DecodeCsvStream::put(const char *text) {
    while (*text) put(*text++);
}
void DecodeCsvStream::hex(uint32_t value, unsigned digits) {
    static constexpr char Digits[] = "0123456789ABCDEF";
    for (unsigned i = digits; i-- > 0;) put(Digits[(value >> (4 * i)) & 15u]);
}
void DecodeCsvStream::integer(uint64_t value) {
    char text[24];
    if (Decimal::integer(value, text, sizeof text)) put(text);
}
void DecodeCsvStream::signedInteger(int64_t value) {
    if (value < 0) put('-');
    integer(value < 0 ? uint64_t(0) - uint64_t(value) : uint64_t(value));
}
// Printable ASCII 32..126 as is, except the backslash (doubled) and, in a quoted CSV field, the
// quote (doubled, RFC 4180); everything else as \xHH. No CR or LF is ever written raw.
void DecodeCsvStream::escaped(uint8_t c, bool quoted) {
    if (c == '\\') put("\\\\");
    else if (quoted && c == '"') put("\"\"");
    else if (c >= 32 && c <= 126) put(char(c));
    else { put("\\x"); hex(c, 2); }
}
void DecodeCsvStream::keyLine(const char *key) {
    put("# ");
    put(key);
    put('=');
}
// Signed seconds of a sample from the time origin: |index - origin| * den / num, exact integer part
// and 12 decimals truncated toward zero. |index - origin| < 2^32 and den < 2^32, so the product fits
// 64 bits for every Fs; an invalid Fs leaves the cell empty.
void DecodeCsvStream::time(uint32_t index) {
    const Rational &fs = set_->samplesPerSecond;
    if (!fs.valid()) return;
    const int64_t delta = int64_t(index) - int64_t(origin_);
    const uint64_t magnitude = delta < 0 ? uint64_t(-delta) : uint64_t(delta);
    char text[48];
    if (!Decimal::fraction(magnitude * fs.denominator, fs.numerator, text, sizeof text, 12)) return;
    if (delta < 0) put('-');
    put(text);
}
bool DecodeCsvStream::metadata() {
    const ResultSet &s = *set_;
    const auto &key = s.key();
    enum : uint16_t {
        Format, Firmware, ProtocolLine, StatusLine, Generation, Sequence, Timestamp, Origin, Samples, Fingerprint,
        SettingsHash, FsNumerator, FsDenominator, FsHz, Reference, Trigger, Window, Events, Lost, Dropped, Overflows,
        Clipped, Glitches, Skipped, Processed, Decoded, LineItems};
    switch (item_) {
    case Format: put("# format=openosc-decode-events-csv-v2\r\n"); break;
    case Firmware:
        // The one text of the caller's: streamed a character at a time (escaped, up to 4 bytes each).
        if (!firmware_[0]) break;
        if (!element_) keyLine("firmware");
        if (firmware_[element_]) { escaped(uint8_t(firmware_[element_++]), false); return true; }
        put("\r\n");
        element_ = 0;
        break;
    case ProtocolLine: keyLine("protocol"); put(Protocols::key(s.protocol())); put("\r\n"); break;
    case StatusLine: keyLine("decode_status"); put(StatusNames[unsigned(s.status)]); put("\r\n"); break;
    case Generation: keyLine("record_generation"); integer(key.generation); put("\r\n"); break;
    case Sequence: keyLine("record_sequence"); integer(key.sequence); put("\r\n"); break;
    case Timestamp: keyLine("record_timestamp_us"); integer(key.timestampUs); put("\r\n"); break;
    case Origin: keyLine("record_origin"); integer(key.origin); put("\r\n"); break;
    case Samples: keyLine("record_samples"); integer(key.samples); put("\r\n"); break;
    case Fingerprint: keyLine("record_fingerprint"); put("0x"); hex(key.fingerprint, 8); put("\r\n"); break;
    case SettingsHash: keyLine("settings_hash"); put("0x"); hex(s.settingsHash(), 8); put("\r\n"); break;
    case FsNumerator: keyLine("fs_numerator"); integer(s.samplesPerSecond.numerator); put("\r\n"); break;
    case FsDenominator: keyLine("fs_denominator"); integer(s.samplesPerSecond.denominator); put("\r\n"); break;
    case FsHz: {
        keyLine("fs_hz");
        char text[48];
        if (s.samplesPerSecond.valid() &&
            Decimal::fraction(s.samplesPerSecond.numerator, s.samplesPerSecond.denominator, text, sizeof text, 6))
            put(text);
        put("\r\n");
        break;
    }
    case Reference: keyLine("time_reference"); put(trigger_ ? "trigger" : "start"); put("\r\n"); break;
    case Trigger:
        if (trigger_) { keyLine("trigger_index"); integer(origin_); put("\r\n"); }
        break;
    case Window: keyLine("edge_uncertainty_points"); integer(s.window); put("\r\n"); break;
    case Events: keyLine("events"); integer(s.count()); put("\r\n"); break;
    case Lost: keyLine("lost_descriptors"); integer(s.lost()); put("\r\n"); break;
    case Dropped: keyLine("dropped_units"); integer(s.dropped); put("\r\n"); break;
    case Overflows: keyLine("window_overflows"); integer(s.overflows); put("\r\n"); break;
    case Clipped: keyLine("clipped_points"); integer(s.clipped); put("\r\n"); break;
    case Glitches: keyLine("glitches"); integer(s.glitches); put("\r\n"); break;
    case Skipped: keyLine("skipped"); integer(s.skipped); put("\r\n"); break;
    case Processed: keyLine("processed_samples"); integer(s.processed); put("\r\n"); break;
    case Decoded: keyLine("decoded_samples"); integer(s.samples); put("\r\n"); break;
    default: {
        // Five lines per source line: role, channel, threshold, hysteresis and its mode (the last three
        // when known).
        const unsigned line = (item_ - LineItems) / 5, part = (item_ - LineItems) % 5;
        if (line >= lineCount_) return false;
        const auto &l = lines_[line];
        static constexpr const char *Keys[] = {"_role", "_channel", "_threshold_mv", "_hysteresis_mv", "_hysteresis_mode"};
        if (part >= 2 && !l.threshold) break;
        put("# line");
        integer(line + 1);
        put(Keys[part]);
        put('=');
        if (part == 0) for (const char *c = l.role; *c; ++c) escaped(uint8_t(*c), false);
        else if (part == 1) { put("CH"); integer(l.channel + 1u); }
        else if (part == 2) signedInteger(l.thresholdMv);
        else if (part == 3) integer(l.hysteresisMv);
        else put(l.hysteresisAuto ? "auto" : "manual");
        put("\r\n");
        break;
    }
    }
    ++item_;
    return true;
}
bool DecodeCsvStream::lastOfGroup() const {
    const DecodeEvent &e = set_->event(event_);
    if (event_ + 1u >= set_->count()) return true;
    const DecodeEvent &n = set_->event(event_ + 1u);
    return !((n.flags & DecodeEvent::Continued) && n.kind == e.kind && groupOf(n) == groupOf(e));
}
bool DecodeCsvStream::cell() {
    const DecodeEvent &e = set_->event(event_);
    if (!element_ && column_) put(',');
    bool complete = true;
    switch (column_) {
    case FlagNames: { // One flag per piece, '|' between the names.
        const unsigned bit = element_;
        if (e.flags & (1u << bit)) {
            if (e.flags & ((1u << bit) - 1u)) put('|');
            put(FlagName[bit]);
        }
        complete = bit == 15;
        break;
    }
    case DataHex: // One element per piece, a space between: "48 65" or "01FF 0123".
        if (e.count) {
            if (element_) put(' ');
            hex(element(*set_, e, element_), 2 * elementBytes(e.format));
            complete = element_ + 1u == e.count;
        }
        break;
    case DataAscii: // Quoted, bytes only; one byte per piece.
        if (e.count && elementBytes(e.format) == 1) {
            if (!element_) put('"');
            escaped(uint8_t(element(*set_, e, element_)), true);
            complete = element_ + 1u == e.count;
            if (complete) put('"');
        }
        break;
    default: simple(column_, e); break;
    }
    if (!complete) { ++element_; return false; }
    element_ = 0;
    if (++column_ < Columns) return false;
    put("\r\n");
    column_ = 0;
    return true;
}
bool DecodeCsvStream::simple(unsigned column, const DecodeEvent &e) {
    const unsigned protocol = protocolOf(e.kind);
    const bool i2c = e.kind == I2cTransaction, last = grouped(e) && lastOfGroup();
    // A CAN frame: every column from the one view (checked at begin(), so always readable here).
    CanFdView v;
    const bool can = e.kind == CanFrame && CanFdView::of(*set_, event_, v), fd = can && v.fd;
    // A frame stored as Classical without a known DLC may have ended before its FDF bit (or inside its
    // ID): its format, ID, IDE and RTR are not known, so those cells stay empty (v2; v1 printed them).
    const bool known = can && (fd || v.dlcKnown);
    // ACK seen dominant (see canAcked); an FD frame also through its whole tail.
    const bool acked = can && canAcked(e) && (!fd || v.stop == canfd::Stop::Complete);
    const unsigned crcBits = !known ? 0u : !fd ? 15u : v.dlcKnown ? (v.dlc > 10 ? 21u : 17u) : 0u;
    switch (column) {
    case EventIndex: integer(event_); break;
    case Group: if (grouped(e)) integer(groupOf(e)); break;
    case Cont: put(e.flags & DecodeEvent::Continued ? '1' : '0'); break;
    case GroupLast: if (grouped(e)) put(last ? '1' : '0'); break;
    case StartSample: integer(e.first); break;
    case EndSample: integer(e.last); break;
    case StartS: time(e.first); break;
    case EndS: time(e.last); break;
    case TimeRef: put(trigger_ ? "trigger" : "start"); break;
    case ProtocolName: put(Protocols::key(set_->protocol())); break;
    case KindName: put(kindName(e.kind)); break;
    case Status: put(TableText::code(e)); break;
    case FlagsHex: hex(e.flags, 4); break;
    case IdHex:
        if (i2c) hex(e.field, e.aux & I2cTenBit ? 3 : 2);
        else if (linPid(e)) hex(e.field & 0x3Fu, 2);
        else if (known) hex(v.id, v.extended ? 8 : 3);
        break;
    case IdBits:
        if (i2c) integer(e.aux & I2cTenBit ? 10 : 7);
        else if (linPid(e)) integer(6);
        else if (known) integer(v.extended ? 29 : 11);
        break;
    case LinPid: if (linPid(e)) hex(e.field & 0xFFu, 2); break;
    case ReadWrite: if (i2c) put(e.aux & I2cRead ? 'R' : 'W'); break;
    case ElementBits:
        if (const unsigned bytes = elementBytes(e.format)) {
            const unsigned meta = formatMeta(e.format);
            integer((protocol == 1 || protocol == 3) && meta ? meta : 8 * bytes);
        }
        break;
    case ElementCount: integer(e.count); break;
    case CrcModel:
        if (linPid(e)) put(e.aux & LinEnhanced ? "enhanced" : "classic");
        else if (crcBits) put(crcBits == 15 ? "crc15" : crcBits == 17 ? "crc17" : "crc21");
        break;
    case CrcState:
        // CAN: the CRC sequence alone (a stuff count verdict is in sbc_parity / sbc_value).
        if (e.kind == LinFrame) put(!linVerified(e) ? "not_checked" : e.flags & CheckFailed ? "failed" : "ok");
        else if (can) put(!v.crcBits ? "not_checked" : v.crcReceived == v.crcCalculated ? "ok" : "failed");
        break;
    case CrcReceived: // Whole: 4, 5 or 6 hex digits (CRC-15, -17, -21).
        if (linVerified(e)) hex(e.aux & 0xFFu, 2);
        else if (can && v.crcBits) hex(v.crcReceived, CanFdView::hexDigits(v.crcBits));
        break;
    case CrcCalculated:
        if (linVerified(e)) hex((e.aux >> 8) & 0xFFu, 2);
        else if (can && v.crcBits) hex(v.crcCalculated, CanFdView::hexDigits(v.crcBits));
        break;
    case Ack:
        // I2C: the transaction's summary, on its last segment (earlier ones hold it as it was then).
        if (i2c && last) {
            const unsigned naks = (e.aux >> 16) & 0xFFu;
            if (e.aux & I2cAddressNack) put("addr_nak");
            else if (e.flags & NoAck) put("data_nak");
            else if ((e.aux & I2cRead) && naks) put("read_end_nak"); // The master's normal last NACK.
            else put("ack");
        } else if (can) {
            // nak: the slot was sampled recessive (known on any frame); ack: only a completed frame.
            if (e.flags & NoAck) put("nak");
            else if (acked) put("ack");
        } else if (e.kind == LinFrame && (e.flags & NoAck)) put("no_response");
        break;
    case NakCount: if (i2c && last) integer((e.aux >> 16) & 0xFFu); break;
    case NakFirst:
        if (i2c && last && ((e.aux >> 8) & 0xFFu) != 0xFFu) integer((e.aux >> 8) & 0xFFu);
        break;
    case StartCond: if (i2c) put(e.aux & I2cBeganSr ? "Sr" : "S"); break;
    case EndCond:
        if (i2c && last) put(e.aux & I2cEndedStop ? "P" : e.aux & I2cEndedSr ? "Sr" : "");
        break;
    case CanExtended: if (known) put(v.extended ? '1' : '0'); break;
    case CanRtr: if (known && !fd) put(v.rtr ? '1' : '0'); break; // FD has no RTR (an RRS is can_rrs).
    case CanDlc: if (can && v.dlcKnown) integer(v.dlc); break; // Never a DLC 0 the frame did not carry.
    case CanFd: if (known) put(fd ? '1' : '0'); break;
    // Control bits as far as received: the DLC follows them; a data-clock stop has read BRS (= 1).
    case CanBrs: if (fd && (v.dlcKnown || v.dataPhase || v.stop == canfd::Stop::DataClock)) put(v.brs ? '1' : '0'); break;
    case CanEsi: if (fd && v.dlcKnown) put(v.esi ? '1' : '0'); break;
    case CanRrs: if (fd) put(v.rrsRecessive ? '1' : '0'); break; // Read before FDF.
    case CanRes:
        if (fd && (v.resRecessive || v.dlcKnown || v.stop == canfd::Stop::DataClock)) put(v.resRecessive ? '1' : '0');
        break;
    case CrcBits: if (crcBits) integer(crcBits); break;
    case SbcReceivedBits: // G2 G1 G0 P as received.
        if (fd && v.sbcReceived) for (unsigned b = 4; b-- > 0;) put((v.sbcRaw >> b) & 1u ? '1' : '0');
        break;
    case SbcValue: if (fd && v.sbcReceived) integer(v.sbcValue); break;
    case SbcCalculated: if (fd && v.sbcReceived) integer(v.sbcCalculated); break;
    case SbcParityState: if (fd && v.sbcReceived) put(v.sbcParityOk ? "ok" : "bad"); break;
    case StopField: if (fd) put(StopNames[unsigned(v.stop)]); break;
    // The FD tail as read: only for an acknowledged frame (a recessive slot leaves the delimiter's
    // length unknown, docs/SCOPE_PROTOCOL_DECODERS_CAN_FD.md §6.1).
    case CrcDelimiterBits: if (fd && acked) put(v.crcDelimiter2 ? '2' : '1'); break;
    case AckBits: if (fd && acked) put(v.ack2 ? '2' : '1'); break;
    case SpiPartial: if (e.kind == SpiFrame) integer(e.aux & 31u); break;
    case DominantBits: if (e.kind == CanErrorFlag || e.kind == CanOverload) integer(e.aux); break;
    case AuxHex: hex(e.aux, 8); break;
    default: return false;
    }
    return true;
}
} // namespace openosc::decoding::exporting
