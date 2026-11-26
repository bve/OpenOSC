#include "openosc/decode/csv_export.hpp"

namespace openosc::decoding {
exporting::CsvContext CsvExport::context(const ResultSet &set, const Settings &s, const char *firmware) {
    exporting::CsvContext c;
    c.firmware = firmware;
    c.reference = set.triggered ? exporting::CsvContext::TimeReference::Trigger : exporting::CsvContext::TimeReference::Start;
    c.triggerIndex = set.triggerIndex;
    // The levels this very record was decoded with (ResultSet), never the settings now: each line its
    // own threshold and the hysteresis actually used (Auto: from that line's swing).
    const bool known = set.levelsKnown;
    const auto line = [&](const char *role, unsigned channel, unsigned slot) {
        auto &l = c.lines[c.lineCount++];
        l.role = role; l.channel = uint8_t(channel); l.threshold = known; l.thresholdMv = set.thresholdMv[slot & 1];
        l.hysteresisMv = set.hysteresisMv[slot & 1]; // Exactly what was used: uint32 end to end.
        l.hysteresisAuto = set.hysteresisAuto;
    };
    switch (set.protocol()) {
    case Protocol::Uart: case Protocol::Rs232: line("RX", set.channel, set.channel); break;
    case Protocol::Lin: line("LIN", set.channel, set.channel); break;
    case Protocol::I2c:
        line("SDA", s.i2c.sdaChannel, s.i2c.sdaChannel);
        line("SCL", s.i2c.sdaChannel ^ 1u, s.i2c.sdaChannel ^ 1u);
        break;
    case Protocol::Spi:
        line("SCLK", s.spi.clockChannel, s.spi.clockChannel);
        line("DATA", s.spi.clockChannel ^ 1u, s.spi.clockChannel ^ 1u);
        break;
    case Protocol::Can:
        if (set.difference) { line("CANH", 0, 0); line("CANL", 1, 0); } // One difference: its threshold and hysteresis.
        else line("RXD", set.channel, set.channel);
        break;
    default: break;
    }
    return c;
}
uint32_t CsvExport::begin(Owner owner, ResultSets &sets, const Settings &settings, const char *firmware) {
    refusal_ = Refusal::None;
    if (ticket_ && !held()) cancel(); // A hold revoked meanwhile (Off, a new source): gone already.
    if (ticket_) { refusal_ = Refusal::Busy; return 0; }
    const auto *published = sets.published();
    if (!published || published->status != DecodeStatus::Done) { refusal_ = Refusal::NoResult; return 0; }
    if (!sets.acquire(lease_)) { refusal_ = Refusal::Busy; return 0; }
    sets_ = &sets;
    context_ = context(*published, settings, firmware);
    if (!stream_.begin(*published, context_)) { sets.release(lease_); sets_ = nullptr; refusal_ = Refusal::Format; return 0; }
    ticket_ = ++nextTicket_ ? nextTicket_ : ++nextTicket_; // Never 0.
    owner_ = owner; measured_ = 0; counting_ = false;
    return ticket_;
}
bool CsvExport::held() { return sets_ && sets_->leased(lease_); }
int CsvExport::measure(uint32_t ticket, uint32_t &total) {
    if (!ticket || ticket != ticket_ || !held()) { if (ticket == ticket_) cancel(); return -1; }
    if (!counting_ && measured_) { total = measured_; return 1; }
    counting_ = true;
    uint8_t scratch[64];
    for (uint32_t n = 0; n < MeasureChunk;) {
        const size_t got = stream_.read(scratch, sizeof scratch);
        if (!got) break;
        n += uint32_t(got);
    }
    if (!stream_.done()) return 0;
    measured_ = uint32_t(stream_.written());
    counting_ = false;
    stream_.end(); // The same held set, from its first byte again: the bytes the reader gets.
    if (!stream_.begin(*sets_->leased(lease_), context_)) { cancel(); return -1; }
    total = measured_;
    return 1;
}
size_t CsvExport::read(uint32_t ticket, uint8_t *output, size_t capacity) {
    if (!ticket || ticket != ticket_ || counting_) return 0;
    if (!held()) { cancel(); return 0; } // Revoked: not one more byte of a set that may be rewritten.
    return stream_.read(output, capacity);
}
void CsvExport::end(uint32_t ticket) {
    if (ticket && ticket == ticket_) cancel();
}
void CsvExport::cancel() {
    stream_.end(); // The stream lets go of the set first, then the hold.
    if (sets_) sets_->release(lease_);
    sets_ = nullptr; lease_ = {}; ticket_ = 0; owner_ = Owner::None; counting_ = false; measured_ = 0;
}
} // namespace openosc::decoding
