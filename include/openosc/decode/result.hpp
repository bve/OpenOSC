#pragma once
#include "openosc/decode/record_key.hpp"
#include "openosc/decode/settings.hpp"
#include <cstddef>

namespace openosc::decoding {
// One decoded unit (plan §5.5): record indices, a protocol field (CAN ID, I2C address, LIN PID), an
// auxiliary word (CRC, segment group) and its elements in the set's byte pool, tagged by `format`.
struct DecodeEvent {
    enum Format : uint8_t { None = 0, Bytes = 1, Words16 = 2 }; // Words16: little-endian 16-bit elements.
    enum Flags : uint16_t {
        Open = 1u << 0,       // Its end lies past the record: the width is a lower bound.
        Cut = 1u << 1,        // The protocol unit is incomplete in this record.
        Continued = 1u << 2,  // A further segment of the same group (aux).
        Glitch = 1u << 3,     // Pulses shorter than the minimum were dropped inside it.
    };
    uint32_t first = 0, last = 0;
    uint32_t field = 0, aux = 0;
    uint16_t payload = 0xFFFF;
    uint8_t count = 0, kind = 0;
    uint16_t flags = 0;
    uint8_t format = None, reserved = 0;
};
static_assert(sizeof(DecodeEvent) == 24, "plan §5.5: 24-byte descriptors");
// The status of one record's result (plan §5.3): what the status line says.
// Timing: the protocol's bit (or SPI gap) lies outside the decoder's range at the record's Fs.
// Coupling: a source channel of a protocol that needs DC (Settings::needsDc) was AC coupled.
enum class DecodeStatus : uint8_t { Idle, Decoding, Done, Cancelled, NoScale, OutOfRange, ChannelOff, Unsupported, Timing, Coupling };
// One record's result: descriptors and their byte pool, filled in order, never evicted (plan §5.5).
class ResultSet {
public:
    static constexpr unsigned Events = 512, Pool = 4096;
    void reset(const RecordKey &key, uint32_t settingsHash, Protocol protocol);
    // False once full: the event is counted as lost (EVT>), nothing stored is changed.
    bool append(const DecodeEvent &event);
    // Room for n bytes of an event still to be appended: their pool offset, else 0xFFFF (no pool, or
    // no descriptor left for that event: the caller then appends nothing of it).
    uint16_t reserve(unsigned bytes);
    // Room for n more bytes of the event stored last, right after its elements (count * width bytes
    // from its payload): their offset, else 0xFFFF. Only the pool is checked: a full descriptor table
    // never stops the event it already holds (the 512th gets all its elements). False for an event
    // without a payload, or one whose elements are not the pool's last bytes (another event's bytes
    // came after them): nothing then is moved or overwritten.
    uint16_t extend(unsigned bytes, unsigned width);
    uint8_t *pool(uint16_t offset) { return pool_ + offset; }
    const uint8_t *pool(uint16_t offset) const { return pool_ + offset; }
    // The last stored event (for a decoder completing it), or none.
    DecodeEvent *lastEvent() { return count_ ? &events_[count_ - 1] : nullptr; }
    const DecodeEvent &event(unsigned i) const { return events_[i]; }
    DecodeEvent &event(unsigned i) { return events_[i]; } // The job's set being built only.
    unsigned count() const { return count_; }
    uint32_t lost() const { return lost_; }
    const RecordKey &key() const { return key_; }
    uint32_t settingsHash() const { return settingsHash_; }
    Protocol protocol() const { return protocol_; }
    // The whole record's samples were read (coverage 100 %): `processed` of `samples`.
    uint32_t processed = 0, samples = 0, glitches = 0, skipped = 0;
    // The decoder's own losses and doubts, for the status (plan §5.3): units lost for the pool
    // (EVT> with lost()), bursts past the two lines' window (OVR), difference points at an ADC rail.
    uint32_t dropped = 0, overflows = 0, clipped = 0;
    uint16_t flagsSeen = 0; // Every flag of every event (the status's Fs LOW, ?).
    uint32_t cycles = 0; // CPU cycles of its slices (DWT): the decode speed.
    DecodeStatus status = DecodeStatus::Idle;
    int8_t initialLevel = -1;
    uint8_t channel = 0; // The source channel.
    // The record's time axis, for the table, Detail and the jump (copied at offer: no lease is needed
    // to read it). Plan §5.5: triggered, times are from the trigger; else from the record's start (t0).
    Rational samplesPerSecond;
    uint32_t triggerIndex = 0;
    bool triggered = false;
    uint32_t window = 1; // The edges' uncertainty in points (ProtocolMachine::window): Detail.
    // The levels this very record was decoded with (plan §6.1), for Detail and the CSV: the channels read
    // (a bit each; the CANH-CANL difference: `difference`, its values in [0]), each one's threshold and the
    // hysteresis actually used, in mV at the probe tip: Auto, Protocols::AutoHysteresisPercent of that line's swing in
    // this record (each line its own); Manual, the setting. Known once the decode starts (levelsKnown).
    uint8_t lines = 0;
    bool difference = false, hysteresisAuto = false, levelsKnown = false;
    int32_t thresholdMv[2] = {0, 0};
    uint32_t hysteresisMv[2] = {0, 0};
    // Auto: the swing is being measured (a bounded pass before the decode; `processed` counts it).
    bool measuring = false;
    uint32_t timeOrigin() const { return triggered ? triggerIndex : 0; }
private:
    DecodeEvent events_[Events];
    uint8_t pool_[Pool];
    RecordKey key_;
    uint32_t settingsHash_ = 0, lost_ = 0;
    unsigned count_ = 0, used_ = 0;
    Protocol protocol_ = Protocol::Off;
};
// An immutable hold of one published result (plan §5.4: export). Only its ticket is handed out; the
// set itself is reached through ResultSets::leased(), which says when the hold is gone.
struct ResultLease {
    uint8_t slot = 0xFF;
    uint32_t ticket = 0;
    bool valid() const { return slot < 2; }
};
// Two result sets (plan §5.4): one published to the UI, the other filled by the job; publish() swaps
// their roles. The published set is never written until it has been replaced. One of them may be
// leased (an export): while the building slot is the leased one, the job takes no record
// (buildingFree; backpressure on the decoder only, never on the capture), so the leased set stays
// exactly as it was published until it is released or revoked (Off, a new source).
class ResultSets {
public:
    ResultSet &building() { return sets_[1 - published_]; }
    const ResultSet &building() const { return sets_[1 - published_]; }
    const ResultSet *published() const { return hasPublished_ ? &sets_[published_] : nullptr; }
    void publish() { published_ = 1 - published_; hasPublished_ = true; ++revision_; }
    void clear() { hasPublished_ = false; ++revision_; }
    uint32_t revision() const { return revision_; } // Changes with every publish or clear.
    // The published set, held: false when none is published or one is held already.
    bool acquire(ResultLease &out);
    void release(const ResultLease &lease);                   // Only the current ticket releases.
    void revoke() { ++ticket_; leased_ = 0xFF; }              // Every hold gone (Off, invalidate).
    const ResultSet *leased(const ResultLease &lease) const; // nullptr once released or revoked.
    bool buildingFree() const { return leased_ != 1 - published_; }
    bool held() const { return leased_ != 0xFF; }
private:
    ResultSet sets_[2];
    unsigned published_ = 0;
    uint32_t revision_ = 0, ticket_ = 0;
    uint8_t leased_ = 0xFF;
    bool hasPublished_ = false;
};
} // namespace openosc::decoding
