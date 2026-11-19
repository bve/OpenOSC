#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace openosc {
// Long main-loop passes and Auto frame gaps, with what ran or held then (SYST:STALl?).
class StallLog {
public:
    enum Cause : uint8_t { Save = 1, Theme = 2, Guard = 4, Quiet = 8, Paused = 16, Supplied = 32, Frame = 64 };
    static constexpr uint32_t PassMs = 50, GapMs = 1000;
    static constexpr unsigned Entries = 8;
    struct Entry { uint32_t at, ms; uint8_t causes; };

    void mark(uint8_t causes) { pass_ |= causes; }
    void pass(uint32_t start, uint32_t end) {
        if (end - start >= PassMs) add(start, end - start, pass_);
        pass_ = 0;
    }
    // expecting: frames are due now (Scope, Auto, running). state: Quiet/Paused/Supplied at this pass.
    // recordMs: one record's duration; Auto frames come up to two of them apart (capture, then its timeout).
    void frame(uint32_t sequence, bool expecting, uint8_t state, uint32_t now, uint32_t recordMs = 0) {
        if (!expecting) { frameAt_ = now; gap_ = 0; sequence_ = sequence; return; }
        gap_ |= state;
        if (sequence == sequence_) return;
        if (now - frameAt_ >= GapMs + 2 * uint64_t(recordMs)) add(frameAt_, now - frameAt_, uint8_t(gap_ | Frame));
        sequence_ = sequence; frameAt_ = now; gap_ = 0;
    }
    // "count,longest_ms[,at_ms:ms:causes]...", newest last, causes S T G Q P U F; whole entries, oldest dropped first.
    size_t text(char *out, size_t size) const {
        if (!size) return 0;
        int n = std::snprintf(out, size, "%lu,%lu", (unsigned long)count_, (unsigned long)longest_);
        if (n < 0 || size_t(n) >= size) { out[0] = 0; return 0; }
        const unsigned kept = count_ < Entries ? unsigned(count_) : Entries;
        char parts[Entries][32];
        size_t lengths[Entries], total = size_t(n);
        for (unsigned i = 0; i < kept; ++i) {
            const Entry &e = entries_[(count_ - kept + i) % Entries];
            char causes[8]; unsigned k = 0;
            for (unsigned b = 0; b < 7; ++b) if (e.causes & (1U << b)) causes[k++] = "STGQPUF"[b];
            causes[k] = 0;
            const int m = std::snprintf(parts[i], sizeof parts[i], ",%lu:%lu:%s", (unsigned long)e.at, (unsigned long)e.ms, causes);
            lengths[i] = m > 0 ? size_t(m) : 0;
        }
        unsigned first = kept;
        while (first && total + lengths[first - 1] < size) total += lengths[--first];
        for (unsigned i = first; i < kept; ++i) { std::memcpy(out + n, parts[i], lengths[i]); n += int(lengths[i]); }
        out[n] = 0;
        return size_t(n);
    }
private:
    void add(uint32_t at, uint32_t ms, uint8_t causes) {
        entries_[count_ % Entries] = {at, ms, causes};
        ++count_;
        if (ms > longest_) longest_ = ms;
    }
    Entry entries_[Entries]{};
    uint32_t count_ = 0, longest_ = 0, sequence_ = 0, frameAt_ = 0;
    uint8_t pass_ = 0, gap_ = 0;
};
} // namespace openosc
