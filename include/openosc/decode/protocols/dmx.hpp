#pragma once
#include "openosc/decode/protocols/uart.hpp"

namespace openosc::decoding::protocols {
// DMX512 (ANSI E1.11-2024, GitHub #163) on one logic line, MARK = 1: the RS-485 pair's CH1 - CH2 or a
// receiver's RO, through the threshold stream. Slots are 250 kbaud 8N2, LSB first (table 5), each one
// sampled by CharacterReceiver from its START. Receiver limits of table 7:
//   BREAK  a space of at least 88 us: a new packet (shorter spaces are characters; one that holds the
//          line at space through the stop bits but is shorter than a BREAK is a framing error);
//   MAB    at least 8 us before the START code, else SyncError ("TIM"; the slots are still decoded);
//   BREAK to BREAK at least 1196 us, else SyncError on the packet before.
// A packet (DmxPacket) runs from its BREAK to its last slot: field bits 0-7 the START code (slot 0),
// bits 8-27 the MAB in points; aux bits 0-19 the BREAK in points, 24-31 the segment group; the data
// slots 1..512 are its elements (CONT segments of 255, every segment with the same field and aux).
// Any number of data slots is a packet (8.7: no minimum). Slots past 512 are not stored: CheckFailed
// ("LEN"). A slot with a stop bit at space (either of the two, 9.1) ends the packet with Framing at
// that slot, which is discarded with every later slot; decoding then waits for the next BREAK. The
// record's end inside a slot or a space: Cut; idle after the last slot: Open (more slots may follow).
// A BREAK whose START code never came (another BREAK, the record's end): DmxBreak (aux and field
// as a packet's). A BREAK from the record's first point: Note, its length a lower bound. Activity
// before the first BREAK: one Cut DmxFragment.
class DmxDecoder final : public ProtocolDecoder {
public:
    static constexpr uint32_t Baud = 250000, BreakMinNs = 88000, MabMinNs = 8000, BreakToBreakMinNs = 1196000;
    static constexpr unsigned MaximumSlots = 512;
    static constexpr uint32_t SpanMask = 0xFFFFF; // 20 bits: any span of a record (MaximumRing 2^19 points).
    // False (nothing decoded) when a bit is outside 2..2^24 points at this Fs.
    bool configure(bool inverted, const Rational &samplesPerSecond);
    void start(ResultSet &result, int initialLevel) override;
    void feed(const Transition *transitions, unsigned count, ResultSet &result) override;
    void finish(uint32_t samples, ResultSet &result) override;
    uint32_t dropped() const { return writer_.dropped(); }
    static unsigned startCode(const DecodeEvent &e) { return e.field & 0xFFu; }
    static uint32_t mabPoints(const DecodeEvent &e) { return e.field >> 8 & SpanMask; }
    static uint32_t breakPoints(const DecodeEvent &e) { return e.aux & SpanMask; }
    // The number (1..512) of the first data slot of segment `index`, and the data slots of its packet.
    static unsigned firstSlot(const ResultSet &set, unsigned index) {
        unsigned first = 1;
        for (unsigned i = groupStart(set, index); i < index; ++i) first += set.event(i).count;
        return first;
    }
    static unsigned slots(const ResultSet &set, unsigned index) {
        unsigned i = groupStart(set, index), n = set.event(i).count;
        while (++i < set.count() && continues(set.event(i - 1), set.event(i))) n += set.event(i).count;
        return n;
    }
private:
    static bool continues(const DecodeEvent &previous, const DecodeEvent &event) {
        return (event.flags & DecodeEvent::Continued) && previous.kind == event.kind && groupOf(previous) == groupOf(event);
    }
    static unsigned groupStart(const ResultSet &set, unsigned index) {
        while (index && continues(set.event(index - 1), set.event(index))) --index;
        return index;
    }
    enum class State : uint8_t { Hunt, Mab, Slots };
    void edge(uint32_t index, int level, ResultSet &result);
    void rise(uint32_t index, ResultSet &result);
    void onBreak(uint32_t first, uint32_t end, ResultSet &result);
    void character(ResultSet &result);
    void close(uint32_t last, uint16_t flags, ResultSet &result); // The open packet, or a BREAK without one.
    void single(ResultSet &result, uint8_t kind, uint32_t first, uint32_t last, uint16_t flags, uint32_t mab);
    void fragment(uint32_t end, ResultSet &result);
    uint16_t base() const { return clock_.low() ? uint16_t(FsLow) : uint16_t(0); }
    BitClock clock_;
    CharacterFormat format_;
    CharacterReceiver rx_;
    EventWriter writer_;
    uint32_t breakMin_ = 0, mabMin_ = 0, spacingMin_ = 0;
    uint32_t spaceSince_ = 0, breakStart_ = 0, breakPoints_ = 0, mabStart_ = 0, mab_ = 0, lastEnd_ = 0, lastBreak_ = 0, fragmentFirst_ = 0;
    int level_ = -1;
    State state_ = State::Hunt;
    uint8_t packetGroup_ = 0;
    bool configured_ = false, inverted_ = false, atStart_ = false, synced_ = false, fragment_ = false;
    bool reset_ = false;      // A BREAK whose packet is not closed yet (MAB, slot 0, the slots).
    bool packet_ = false;     // Its START code came: the packet's unit is open.
    bool spaceChar_ = false;  // A character at space through its stop bits: a BREAK or framing, told at its end.
    bool overflow_ = false, tail_ = false, breakNote_ = false, lastBreakKnown_ = false, packetStored_ = false;
};
} // namespace openosc::decoding::protocols
