#pragma once
// Diagnostic builds only (LocalControl, InterleaveProbeControl): the SWD mailbox records' shared rules.
#include "openosc/acquisition/frame.hpp"
#include "openosc/update/crc32.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace openosc::diag {
template <typename Record> uint32_t checksum(const Record &record) {
    return Crc32::compute(&record, sizeof record - sizeof(uint32_t)); // Every word but the closing check.
}
// Odd while written, even and larger once complete: the host never takes a half-written record.
template <typename Record> void publish(volatile uint32_t &word, volatile Record &target, Record record, uint32_t &counter) {
    record.check = checksum(record);
    word = ++counter;
    std::atomic_thread_fence(std::memory_order_seq_cst);
    const auto *from = reinterpret_cast<const uint32_t *>(&record);
    auto *to = reinterpret_cast<volatile uint32_t *>(&target);
    for (size_t i = 0; i < sizeof record / sizeof(uint32_t); ++i) to[i] = from[i];
    std::atomic_thread_fence(std::memory_order_seq_cst);
    word = ++counter;
}
// Where a leased record's raw words are, as a host reads them over SWD: its metadata, the lane banks
// (AHB), and for a split ring the older words in AXI SRAM (FrameView::split). `Record` has these fields.
template <typename Record> void frameFields(const scope::FrameView &view, Record &d) {
    const auto &m = view.metadata();
    const auto &split = view.split();
    d.frameValid = 1;
    d.sequence = m.sequence; d.samples = m.samples; d.ring = m.ring; d.origin = m.origin; d.layout = uint32_t(m.layout);
    d.rateNumerator = m.samplesPerSecond.numerator; d.rateDenominator = m.samplesPerSecond.denominator;
    d.flags = uint32_t(m.channels[0].enabled) | uint32_t(m.channels[1].enabled) << 1 | uint32_t(m.triggered) << 2 |
              uint32_t(m.rolling) << 3 | uint32_t(m.timingVerified) << 4;
    d.triggerIndex = m.triggerIndex;
    for (unsigned i = 0; i < 4; ++i) {
        d.banks[i] = uint32_t(reinterpret_cast<uintptr_t>(view.bank(i)));
        d.older[i] = uint32_t(reinterpret_cast<uintptr_t>(split.older[i]));
        d.end[i] = split.end[i]; d.newest[i] = split.newest[i];
    }
    d.ahbWords = split.ahbWords; d.historyWords = split.historyWords;
}
} // namespace openosc::diag
