#include "openosc/generator/arbitrary.hpp"

namespace openosc::generator {
unsigned ArbitraryBanks::tableBank(unsigned active, unsigned playing) const {
    (void)playing; // A table may replace the one playing (after its halt): only the loaded waveform's bank is kept.
    const unsigned order[] = {active ^ 1U, active};
    for (const unsigned bank : order)
        if (bank != loaded_ && !(owner_ && !lost_ && bank == stage_)) return bank;
    return loaded_ == order[0] ? order[1] : order[0]; // Only the stage's bank is free: the stage is lost.
}
ArbitraryBanks::Stage ArbitraryBanks::begin(const void *owner, unsigned active, unsigned playing) {
    if (!owner) return Stage::Busy;
    if (owner_) return owner_ == owner ? Stage::Pending : Stage::Busy;
    const unsigned order[] = {active ^ 1U, active};
    for (const unsigned bank : order) {
        if (bank == loaded_ || bank == playing) continue;
        owner_ = owner; stage_ = bank; lost_ = false;
        return Stage::Ready;
    }
    return Stage::Conflict;
}
bool ArbitraryBanks::put(const void *owner, unsigned index, uint16_t code) {
    if (!holds(owner) || lost_ || index >= Points || code > ArbitraryLimits::MaximumCode) return false;
    tables_[stage_][index] = code;
    return true;
}
bool ArbitraryBanks::commit(const void *owner, unsigned points, ArbitrarySource source, Undo &undo) {
    if (!holds(owner) || lost_ || !ArbitraryLimits::points(points) || source == ArbitrarySource::None) return false;
    undo = {loaded_, points_, source_};
    loaded_ = stage_; points_ = uint16_t(points); source_ = source; ++revision_;
    owner_ = nullptr;
    return true;
}
void ArbitraryBanks::rollback(const Undo &undo) {
    loaded_ = undo.bank; points_ = undo.points; source_ = undo.source; ++revision_; // Readers of the new one stop.
}
void ArbitraryBlock::byte(uint8_t value, CodeSink &sink) {
    if (!(at_++ & 1)) { low_ = value; return; }
    if (error_) return; // The rest of the block is read to its end, never written.
    const int16_t code = int16_t(uint16_t(low_) | uint16_t(value) << 8);
    if (code < 0 || code > int16_t(ArbitraryLimits::MaximumCode)) error_ = -222;
    else if (!sink.put(at_ / 2 - 1, uint16_t(code))) error_ = -200;
}
const char *ArbitraryCsv::text(Error error) {
    switch (error) {
    case Error::Binary: return "not a text file";
    case Error::Line: return "line too long";
    case Error::Syntax: return "not a DAC code";
    case Error::Range: return "code above 4095";
    case Error::TooMany: return "more than 1024 codes";
    case Error::TooFew: return "fewer than 2 codes";
    case Error::Size: return "file larger than 64 KiB";
    case Error::Stage: return "waveform memory taken";
    default: return "";
    }
}
bool ArbitraryCsv::endLine(CodeSink &sink) {
    unsigned begin = 0, end = used_;
    while (begin < end && (text_[begin] == ' ' || text_[begin] == '\t')) ++begin;
    while (end > begin && (text_[end - 1] == ' ' || text_[end - 1] == '\t')) --end;
    used_ = 0;
    if (begin == end || text_[begin] == '#') return true; // Blank or a comment.
    const char first = text_[begin];
    if (!points_ && !header_ && ((first >= 'A' && first <= 'Z') || (first >= 'a' && first <= 'z'))) { header_ = true; return true; }
    uint32_t code = 0;
    for (unsigned i = begin; i < end; ++i) {
        if (text_[i] < '0' || text_[i] > '9' || i - begin >= 5) return fail(Error::Syntax);
        code = code * 10 + unsigned(text_[i] - '0');
    }
    if (code > ArbitraryLimits::MaximumCode) return fail(Error::Range);
    if (points_ >= ArbitraryLimits::MaximumPoints) return fail(Error::TooMany);
    if (!sink.put(points_, uint16_t(code))) return fail(Error::Stage);
    ++points_;
    return true;
}
bool ArbitraryCsv::feed(const char *data, size_t size, CodeSink &sink) {
    if (error_ != Error::None) return false;
    static constexpr uint8_t Bom[] = {0xef, 0xbb, 0xbf};
    for (size_t i = 0; i < size; ++i) {
        const uint8_t c = uint8_t(data[i]);
        const uint32_t at = bytes_++;
        if (bytes_ > MaximumBytes) return fail(Error::Size);
        if (at < 3 && c == Bom[at] && (at == 0 || bom_)) { bom_ = true; continue; }
        if (at < 3 && bom_) return fail(Error::Binary); // A byte order mark cut short.
        if (carriage_ && c != '\n') return fail(Error::Binary); // A CR alone.
        carriage_ = false;
        if (c == '\r') { carriage_ = true; continue; }
        if (c == '\n') {
            if (!endLine(sink)) return false;
            ++line_;
            continue;
        }
        if ((c < 0x20 && c != '\t') || c >= 0x7f) return fail(Error::Binary);
        if (used_ >= LineBytes) return fail(Error::Line);
        text_[used_++] = char(c);
    }
    return true;
}
bool ArbitraryCsv::finish(CodeSink &sink) {
    if (error_ != Error::None) return false;
    if (carriage_) return fail(Error::Binary);
    if (bom_ && bytes_ < 3) return fail(Error::Binary);
    if (used_ && !endLine(sink)) return false;
    return ArbitraryLimits::points(points_) || fail(Error::TooFew);
}
} // namespace openosc::generator
