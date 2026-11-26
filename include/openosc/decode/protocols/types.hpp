#pragma once
#include "openosc/decode/decoder.hpp"

// Shared vocabulary of the protocol state machines (docs/SCOPE_PROTOCOL_DECODERS_PROTOCOLS.md): event
// kinds, flags and the payload format byte, the bit clock, the event writer and the two-line coincidence
// queue. None of it changes the base contract of decoder.hpp / result.hpp.
namespace openosc::decoding::protocols {
// DecodeEvent::kind: protocol in the high nibble (1 UART .. 6 DMX512), the type in the low one. The base
// kinds (EdgeRise 1, EdgeFall 2) stay below 16.
enum Kind : uint8_t {
    UartData = 0x10,     // Characters (a group of them, see UartOptions::groupIdleBits).
    UartBreak = 0x11,    // The line held at space for at least one whole character.
    UartFragment = 0x12, // Activity whose character start is not in the record (Cut).
    I2cTransaction = 0x20, // START/Sr .. Sr/STOP: address (field), data bytes, ACK summary (aux).
    I2cFragment = 0x21,  // Clocked bits before the record's first START (Cut).
    SpiFrame = 0x30,     // Words between two SCLK pauses (or from the manual start).
    LinFrame = 0x40,     // Break .. checksum: PID (field), data, checksum (aux).
    LinFragment = 0x41,  // Activity before the record's first break (Cut).
    CanFrame = 0x50,     // SOF .. EOF (or the error point): ID (field), data, CRC (aux).
    CanErrorFlag = 0x51, // A dominant run of 6..12 bits after an error (active error flag).
    CanOverload = 0x52,  // An overload flag after EOF / in the intermission.
    CanFragment = 0x53,  // Bus activity before the record's first 11 recessive bits (Cut).
    DmxPacket = 0x60,    // BREAK .. last slot: START code (field), data slots 1..512, BREAK and MAB (DmxDecoder).
    DmxBreak = 0x61,     // A BREAK without a START code after it (another BREAK, the record's end).
    DmxFragment = 0x62,  // Activity before the record's first BREAK (Cut).
};
inline unsigned protocolOf(uint8_t kind) { return kind >> 4; } // 1 UART, 2 I2C, 3 SPI, 4 LIN, 5 CAN, 6 DMX512.
// DecodeEvent::flags above the base ones (Open 0, Cut 1, Continued 2, Glitch 3). One bit may name a
// protocol's own case of a common meaning; the status code a table shows is in the comment.
enum Flag : uint16_t {
    Ambiguous = 1u << 4,     // "?": edges of two lines in one uncertainty window, an unaligned SPI word or
                             // a UART start whose idle before it is shorter than a frame.
    FsLow = 1u << 5,         // "Fs LOW": fewer than MinPointsPerBit points a bit.
    Truncated = 1u << 6,     // "EVT>": the storage ran out in or after this event (nothing later kept).
    Framing = 1u << 7,       // "FE" UART/LIN stop bit, "FRM" CAN form, I2C START/STOP inside a byte,
                             // SPI clock not at its CPOL idle level before a frame, LIN short response.
    ParityError = 1u << 8,   // "PE": UART parity, LIN PID parity bits.
    CheckFailed = 1u << 9,   // "CRC" (CAN), "CHK" (LIN), "LEN" (DMX512: more than 512 data slots).
    NotChecked = 1u << 10,   // The checksum or CRC was not verified (unknown length, incomplete).
    NoAck = 1u << 11,        // "NAK" I2C, "ACK" CAN (recessive ACK slot), LIN header without response.
    StuffError = 1u << 12,   // "STF": six equal CAN bits.
    SyncError = 1u << 13,    // "SYN": LIN SYNC not 0x55 at a consistent bit time; "TIM" DMX512 MAB below 8 us
                             // or BREAK to BREAK below 1196 us (ANSI E1.11 table 7).
    LengthUnknown = 1u << 14, // "LEN?": LIN ID without a length: raw bytes to the next break.
    Note = 1u << 15,         // I2C: clock stretched; CAN error/overload: one view of the bus (observation);
                             // CAN frame: DLC above 8 (8 bytes carried, DLC in the format byte); DMX512:
                             // the BREAK began before the record (its length a lower bound).
};
constexpr uint16_t ErrorFlags = Framing | ParityError | CheckFailed | NoAck | StuffError | SyncError;
// DecodeEvent::format: bits 0-2 the element width (DecodeEvent::Format: None, Bytes, Words16), bits 3-7
// a protocol meta value: UART/SPI the bits of each element (4..16), CAN the received DLC (0..15).
// Readers take the width with formatWidth(): `format == Bytes` is no longer the whole test.
inline uint8_t makeFormat(uint8_t width, unsigned meta) { return uint8_t((width & 7u) | ((meta & 31u) << 3)); }
inline uint8_t formatWidth(uint8_t format) { return format & 7u; }
inline unsigned formatMeta(uint8_t format) { return format >> 3; }
inline unsigned elementBytes(uint8_t format) {
    return formatWidth(format) == DecodeEvent::Words16 ? 2u : formatWidth(format) == DecodeEvent::Bytes ? 1u : 0u;
}
// The segment group of a CONT chain (plan §5.5) sits in aux bits 24-31 of I2C, SPI, UART, LIN and DMX512 events.
constexpr unsigned GroupShift = 24;
inline uint8_t groupOf(const DecodeEvent &e) { return uint8_t(e.aux >> GroupShift); }

constexpr uint32_t MinPointsPerBit = 5; // Below it every event is flagged FsLow.

// The bit time of a record in points (Q16): Fs / baud. Positions are counted in milli-bits from an
// origin (a START, SOF or resynchronising edge); milliBits stays below 2^22 (4194 bits) by construction.
struct BitClock {
    uint64_t pointsQ16 = 0;
    // False when Fs or baud is zero, or a bit would be fewer than 2 or more than 2^24 points.
    static bool of(const Rational &fs, uint32_t baud, BitClock &out);
    // From a measured span: `points` covering `bits` bits (LIN SYNC).
    static BitClock measured(uint32_t points, uint32_t bits) { return {(uint64_t(points) << 16) / bits}; }
    uint32_t points(uint32_t milliBits) const { return uint32_t((uint64_t(milliBits) * pointsQ16) / 65536000u); }
    // Whole bits in a span of points, rounded to nearest.
    uint32_t bits(uint32_t span) const { return uint32_t(((uint64_t(span) << 16) + pointsQ16 / 2) / pointsQ16); }
    bool low() const { return pointsQ16 < (uint64_t(MinPointsPerBit) << 16); }
    bool valid() const { return pointsQ16 != 0; }
};
// At finish(): a unit complete by its last sample may end (its last bit's end) past the record; only the
// last stored event can, and it is held to the record's end.
inline void clampToRecord(ResultSet &result, uint32_t samples) {
    if (auto *e = result.lastEvent()) {
        if (e->last > samples) e->last = samples;
        if (e->first > samples) e->first = samples;
    }
}
// Points (rounded up) of a duration in nanoseconds at Fs; 0 when Fs is invalid. Saturates at 2^32-1.
uint32_t pointsOfNs(const Rational &fs, uint32_t ns);

// Flags that describe one segment's place rather than the unit: not carried into a CONT segment.
constexpr uint16_t SegmentFlags = DecodeEvent::Open | DecodeEvent::Cut | DecodeEvent::Continued | DecodeEvent::Glitch | Truncated;
// Writes one protocol unit at a time into a ResultSet (plan §5.5): a header, then its elements one by one
// into the pool right after each other; past 255 elements a CONT segment of the same group follows. A
// CONT segment starts with every flag of the segment before it except SegmentFlags (LEN?, NOT CHECKED,
// an earlier NACK or framing error stay on every later segment), and the same kind, field, aux and
// format. Flags known only at end() (a failed checksum, Cut, Open) are on the last segment: earlier
// segments are already stored and only the last event of the set can be changed. A unit's first element
// is reserve()d with its descriptor and appended; each later one extends the stored event (ResultSet::
// extend: only the pool limits it, so the set's last descriptor takes all of its elements). No eviction: once
// descriptors or pool run out every later unit is lost (ResultSet::lost() counts lost descriptors,
// dropped() the units lost for the pool) and the event stored last gets Truncated; when that event is
// part of the unit being lost (bytes of it are missing) also Cut. The unit being written is always the
// set's last event: nothing else may be appended until end().
class EventWriter {
public:
    void reset() { *this = EventWriter{}; }
    // A new unit (any open one must have ended); its elements are formatWidth(header.format) wide. Once
    // storage is exhausted the unit is counted as lost here and nothing of it is stored.
    void begin(ResultSet &result, const DecodeEvent &header);
    bool active() const { return active_; }
    // The unit's authoritative descriptor: the stored one once an element was written, else the header.
    DecodeEvent &edit(ResultSet &result);
    // One element spanning [first, last] in the record; `value` keeps its low 8 or 16 bits.
    void push(ResultSet &result, uint16_t value, uint32_t first, uint32_t last);
    // Closes the unit at `last` with these flags added. A unit without elements is stored now.
    void end(ResultSet &result, uint32_t last, uint16_t flags = 0);
    // A whole unit at once (no unit open): `count` bytes then `trailerBytes` more in one pool
    // reservation, stored whole or lost whole under the loss rules above (a full descriptor table:
    // lost(); a full pool: dropped() and Truncated on the event stored last; nothing stored after).
    // `count` is the event's elements (at most 255); the trailer follows them in the pool, outside
    // `count`. With neither, no payload. False: the unit was lost.
    bool block(ResultSet &result, const DecodeEvent &header, const uint8_t *bytes, unsigned count,
               const uint8_t *trailer, unsigned trailerBytes);
    unsigned elements() const { return total_; } // Elements of the open unit, all segments.
    uint32_t dropped() const { return dropped_; }
    bool exhausted() const { return exhausted_; }
    uint8_t nextGroup() const { return group_; }
private:
    bool store(ResultSet &result, uint16_t value);
    void lose(ResultSet &result);
    DecodeEvent header_;
    unsigned width_ = 0, total_ = 0;
    uint32_t dropped_ = 0;
    uint8_t group_ = 0;
    bool active_ = false, stored_ = false, segmented_ = false, exhausted_ = false, lostUnit_ = false;
};

// Two-line decoders (I2C, SPI) see a single index-ordered stream of both lines' transitions. An edge of
// one line closer than `window` points to an edge of the other is ambiguous (plan §6.2: their order is
// not known). The queue holds transitions until the stream has moved past their window, so the check
// looks both ways; among equal indices the `first` channel's edge is handed out before the other's.
// More than Size edges inside one window (a burst; at 280 MS/s the window is ~19 points) force the
// oldest out before its window is complete: an edge of the other line may still follow inside it, so a
// forced item is always ambiguous and counted (overflows(): the status's OVR). Order stays the stream's.
class CoincidenceQueue {
public:
    static constexpr unsigned Size = 16;
    enum class Pop : uint8_t { Ready, Full, End }; // Window complete only / queue full / stream ended.
    struct Item { Transition t; bool ambiguous; };
    void reset(uint32_t window, uint8_t firstChannel) { *this = CoincidenceQueue{}; window_ = window; first_ = firstChannel; }
    void push(const Transition &t) { items_[(head_ + count_++) % Size] = t; }
    // Ready: the next transition whose window is complete, else false. Full: the oldest one even with
    // its window open (then ambiguous). End: every one, the stream having ended.
    bool pop(Item &out, Pop mode);
    bool full() const { return count_ == Size; }
    uint32_t overflows() const { return overflows_; }
    // A queued transition of `channel` within the window after `index` (the one just popped), or null.
    const Transition *pending(uint8_t channel, uint32_t index) const;
private:
    Transition items_[Size];
    unsigned head_ = 0, count_ = 0;
    uint32_t window_ = 1, lastIndex_[2] = {0, 0}, overflows_ = 0;
    bool seen_[2] = {false, false};
    uint8_t first_ = 0;
};
} // namespace openosc::decoding::protocols
