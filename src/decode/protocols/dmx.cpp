#include "openosc/decode/protocols/dmx.hpp"

namespace openosc::decoding::protocols {
namespace {
uint32_t span(uint32_t points) { return points > DmxDecoder::SpanMask ? DmxDecoder::SpanMask : points; }
} // namespace
bool DmxDecoder::configure(bool inverted, const Rational &fs) {
    inverted_ = inverted;
    format_ = CharacterFormat{};
    format_.stopHalfBits = 4; // 8N2, LSB first (table 5).
    configured_ = BitClock::of(fs, Baud, clock_);
    if (!configured_) return false;
    breakMin_ = pointsOfNs(fs, BreakMinNs); mabMin_ = pointsOfNs(fs, MabMinNs); spacingMin_ = pointsOfNs(fs, BreakToBreakMinNs);
    return true;
}
void DmxDecoder::start(ResultSet &result, int initialLevel) {
    writer_.reset(); rx_.cancel();
    spaceSince_ = breakStart_ = breakPoints_ = mabStart_ = mab_ = lastEnd_ = lastBreak_ = fragmentFirst_ = 0;
    state_ = State::Hunt;
    synced_ = fragment_ = reset_ = packet_ = spaceChar_ = overflow_ = tail_ = breakNote_ = lastBreakKnown_ = packetStored_ = false;
    level_ = initialLevel < 0 ? -1 : (initialLevel ? 1 : 0) ^ int(inverted_);
    atStart_ = level_ == 0;
    result.initialLevel = int8_t(initialLevel);
}
void DmxDecoder::feed(const Transition *transitions, unsigned count, ResultSet &result) {
    if (!configured_) return;
    for (unsigned i = 0; i < count; ++i) edge(transitions[i].index, (transitions[i].level ? 1 : 0) ^ int(inverted_), result);
}
void DmxDecoder::edge(uint32_t index, int level, ResultSet &result) {
    if (level_ < 0) { level_ = level ^ 1; atStart_ = level_ == 0; } // The level held since the record began.
    if (level == level_) return;
    if (rx_.active() && rx_.advance(index, level_)) character(result);
    else if (level == 0 && rx_.inLaterStop()) { rx_.cutStop(index); character(result); } // START in the second stop bit.
    level_ = level;
    if (level) { rise(index, result); return; }
    spaceSince_ = index;
    if (!synced_) { if (!fragment_) { fragment_ = true; fragmentFirst_ = index; } return; } // Told once a BREAK is seen.
    if (state_ == State::Mab) { mab_ = index - mabStart_; state_ = State::Slots; } // The START code's START bit.
    if (state_ == State::Slots && !rx_.active()) rx_.begin(format_, clock_, index);
}
void DmxDecoder::rise(uint32_t index, ResultSet &result) {
    const bool fromStart = atStart_;
    atStart_ = false;
    if (index - spaceSince_ >= breakMin_) { breakNote_ = fromStart; onBreak(spaceSince_, index, result); return; }
    if (fromStart && !synced_ && !fragment_) { fragment_ = true; fragmentFirst_ = 0; }
    if (spaceChar_) { // At space through the stop bits, shorter than a BREAK: framing (9.1), then the next BREAK.
        spaceChar_ = false;
        close(index, Framing, result);
        state_ = State::Hunt;
    }
}
void DmxDecoder::onBreak(uint32_t first, uint32_t end, ResultSet &result) {
    spaceChar_ = false; rx_.cancel();
    if (reset_) close(packet_ ? lastEnd_ : first, 0, result); // The packet before ends at its last slot.
    fragment(first, result);
    if (lastBreakKnown_ && first - lastBreak_ < spacingMin_ && packetStored_)
        if (auto *e = result.lastEvent(); e && e->kind == DmxPacket && groupOf(*e) == packetGroup_) e->flags |= SyncError;
    lastBreak_ = first; lastBreakKnown_ = !breakNote_; packetStored_ = false;
    synced_ = reset_ = true; overflow_ = tail_ = false;
    state_ = State::Mab;
    breakStart_ = first; breakPoints_ = end - first; mabStart_ = end; mab_ = 0;
}
void DmxDecoder::character(ResultSet &result) {
    if (rx_.falseStart()) { if (!packet_ && state_ == State::Slots) state_ = State::Mab; return; } // A glitch: still the MAB.
    if (rx_.allSpace() && !rx_.stopOk()) { spaceChar_ = true; return; }
    if (!rx_.stopOk()) { close(rx_.end(), Framing, result); state_ = State::Hunt; return; } // 9.1: this slot and the rest.
    tail_ = rx_.incomplete(); // The record ended in its second stop bit: the slot kept, the packet Cut.
    lastEnd_ = rx_.end();
    if (packet_) {
        if (writer_.elements() >= MaximumSlots) overflow_ = true;
        else writer_.push(result, rx_.value(), rx_.start(), rx_.end());
        return;
    }
    DecodeEvent h;
    h.kind = DmxPacket; h.first = h.last = breakStart_;
    h.field = rx_.value() | span(mab_) << 8;
    packetGroup_ = writer_.nextGroup();
    h.aux = span(breakPoints_) | uint32_t(packetGroup_) << GroupShift;
    h.format = makeFormat(DecodeEvent::Bytes, 8);
    h.flags = uint16_t(base() | (mab_ < mabMin_ ? SyncError : 0) | (breakNote_ ? Note : 0));
    writer_.begin(result, h);
    packet_ = true;
}
void DmxDecoder::close(uint32_t last, uint16_t flags, ResultSet &result) {
    if (packet_) {
        writer_.end(result, last, uint16_t(flags | (overflow_ ? CheckFailed : 0)));
        packetStored_ = !writer_.exhausted();
    } else if (reset_) {
        const uint32_t mab = state_ == State::Mab ? last - mabStart_ : mab_;
        single(result, DmxBreak, breakStart_, last, uint16_t(flags | (breakNote_ ? Note : 0)), mab);
    }
    packet_ = reset_ = overflow_ = false;
}
void DmxDecoder::single(ResultSet &result, uint8_t kind, uint32_t first, uint32_t last, uint16_t flags, uint32_t mab) {
    DecodeEvent h;
    h.kind = kind; h.first = first;
    h.flags = uint16_t(flags | base());
    h.field = kind == DmxBreak ? span(mab) << 8 : 0;
    h.aux = (kind == DmxBreak ? span(breakPoints_) : 0) | uint32_t(writer_.nextGroup()) << GroupShift;
    writer_.begin(result, h);
    writer_.end(result, last);
}
void DmxDecoder::fragment(uint32_t end, ResultSet &result) {
    if (fragment_ && fragmentFirst_ < end) single(result, DmxFragment, fragmentFirst_, end, DecodeEvent::Cut, 0);
    fragment_ = false;
}
void DmxDecoder::finish(uint32_t samples, ResultSet &result) {
    if (!configured_) return;
    if (rx_.active() && rx_.advance(samples, level_)) character(result);
    if (rx_.inLaterStop()) { rx_.endOfRecord(samples); character(result); }
    bool cut = tail_ || spaceChar_ || level_ == 0 || !packet_; // In the MAB: the START code is past the record.
    if (rx_.active()) { rx_.cancel(); cut = true; } // A slot goes on past the record.
    if (level_ == 0 && samples - spaceSince_ >= breakMin_) { // A BREAK goes on at the record's end.
        if (reset_) close(packet_ ? lastEnd_ : spaceSince_, 0, result);
        fragment(spaceSince_, result);
        breakPoints_ = samples - spaceSince_;
        single(result, DmxBreak, spaceSince_, samples, uint16_t(DecodeEvent::Open | (atStart_ ? Note : 0)), 0);
    } else {
        if (reset_) close(cut ? samples : lastEnd_, cut ? uint16_t(DecodeEvent::Cut) : uint16_t(DecodeEvent::Open), result);
        if (!synced_ && atStart_ && level_ == 0 && !fragment_) { fragment_ = true; fragmentFirst_ = 0; }
        fragment(samples, result);
    }
    spaceChar_ = false;
    clampToRecord(result, samples);
}
} // namespace openosc::decoding::protocols
