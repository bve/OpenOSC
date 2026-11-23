#include "openosc/acquisition/arena.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/acquisition/history_schedule.hpp"
#include <algorithm>

namespace openosc::scope {
namespace {
alignas(32) uint16_t ahb[AcquisitionArena::BankBytes / 2] __attribute__((section(".acquisition")));
alignas(32) uint16_t axi[AcquisitionArena::Banks - 1][AcquisitionArena::BankBytes / 2] __attribute__((section(".capture_arena")));
alignas(32) uint16_t trialArea[AcquisitionArena::TrialBytes / 2] __attribute__((section(".capture_trial")));
static_assert(AcquisitionArena::Bytes == RecordBudget::Maximum, "The arena holds the largest budget once");
static_assert(AcquisitionArena::RollWindow * 2 <= AcquisitionArena::BankBytes, "A Roll history or snapshot fits one bank");
static_assert(AcquisitionArena::RollRingPoints * 2 == AcquisitionArena::BankBytes, "The Roll DMA ring is bank 0");

void lanes(CaptureBuffer &out, uint16_t *(*base)(unsigned), uint32_t offsetWords) {
    for (unsigned i = 0; i < 4; ++i) out.lanes[i] = base(i) + offsetWords;
}
// Lane i rings through quarter i of bank 0 (AHB SRAM1 holds lanes 0-1, SRAM2 lanes 2-3) in one
// segment: the capture ring of the compact and split geometries and of Roll.
constexpr uint32_t QuarterWords = AcquisitionArena::BankBytes / 2 / 4;
void ahbQuarters(CaptureBuffer &out) {
    for (unsigned i = 0; i < 4; ++i) out.lanes[i] = ahb + i * QuarterWords;
    out.laneTransfers = out.segmentTransfers = QuarterWords;
}
static_assert(AcquisitionArena::RollRingPoints == 4 * QuarterWords, "Roll rings through the quarters of bank 0");
} // namespace
bool CaptureBuffer::valid() const {
    const uint32_t burst = words() ? 4 : 8; // INCR4 of halfwords, packed for bytes.
    if (!segmentTransfers || segmentTransfers > 65535 || laneTransfers % segmentTransfers || segmentTransfers % burst ||
        laneTransfers > AcquisitionArena::MaximumLaneTransfers)
        return false;
    for (auto *lane : lanes) if (!lane) return false;
    if (!split()) return !copyTransfers;
    if (!HistorySchedule::valid(*this) || ringPoints() > FrameMetadata::MaximumRing) return false;
    for (auto *lane : history) if (!lane) return false;
    return true;
}
uint32_t CaptureBuffer::capacityTransfers() const { return split() ? HistorySchedule::capacity(*this) : transferRing(); }
uint16_t *AcquisitionArena::bank(unsigned index) { return !index ? ahb : index < Banks ? axi[index - 1] : nullptr; }
uint16_t *AcquisitionArena::trialMemory() { return trialArea; }
uint16_t *AcquisitionArena::rollHistory() { return bank(1); }
uint16_t *AcquisitionArena::rollSnapshot() { return bank(2); }
uint16_t *AcquisitionArena::rollSpare() { return bank(3); }
unsigned AcquisitionArena::slots(uint32_t budget, Geometry geometry) {
    // A balanced two-channel slot takes a half of AHB for its two AHB lanes: 128K and less.
    if (geometry == Geometry::Balanced) return budget <= Bytes / 4 ? 2 : 1;
    // Split and compact lanes ring through the whole of bank 0. A half of it would be a ring of 32768
    // points per channel: a 64K record and no room for the full-speed headroom.
    if (geometry == Geometry::Split || geometry == Geometry::Compact) return 1;
    return budget * 2 <= Bytes ? 2 : 1;
}
AcquisitionArena::Geometry AcquisitionArena::geometry(uint32_t budget, bool ch1, bool ch2, bool fast) {
    // Two fast channels up to 128K take Compact, above it Split: both write only AHB SRAM with the
    // capture DMA. Below half the full rate two channels of 64K stay balanced (two slots, the full
    // 32768 points), larger budgets standard.
    if (RecordBudget::channels(ch1, ch2) != 2) return Geometry::Standard;
    if (fast) return budget <= BankBytes ? Geometry::Compact : Geometry::Split;
    return budget <= RecordBudget::Minimum ? Geometry::Balanced : Geometry::Standard;
}
bool AcquisitionArena::record(uint32_t budget, bool ch1, bool ch2, unsigned slot, CaptureBuffer &out, Geometry geometry,
                              bool interleaved) {
    const unsigned channels = RecordBudget::channels(ch1, ch2);
    if (interleaved && (!ch1 || ch2)) return false;
    const uint32_t depth = RecordBudget::perChannel(budget, channels);
    // One channel's bytes are always standard; interleaved words take the geometry asked for.
    const Geometry used = channels == 2 || interleaved ? geometry : Geometry::Standard;
    // Interleaving exists only for the full word rate, which needs every lane in AHB (fullRate).
    if (interleaved && used != Geometry::Compact && used != Geometry::Split) return false;
    const unsigned count = slots(budget, used);
    // A compact ring holds 65536 points per channel: only for the budgets geometry() gives it, so a
    // larger record is never shortened to it unnoticed.
    if (!depth || slot >= count || (used == Geometry::Compact && budget > BankBytes)) return false;
    CaptureBuffer result;
    result.format = interleaved ? SampleFormat::InterleavedWords : channels == 2 ? SampleFormat::Words : SampleFormat::Bytes;
    result.byteChannel = ch2 && !ch1 ? Channel::Ch2 : Channel::Ch1;
    result.region = count == 1 ? Slots : uint8_t(slot ? SlotB : SlotA);
    if (used == Geometry::Split || used == Geometry::Compact) {
        ahbQuarters(result);
        if (used == Geometry::Split) {
            // Lane i's history: words i*49152.. of banks 1-3 (one AXI block), so the ring spans the arena.
            const uint32_t history = (Banks - 1) * QuarterWords;
            for (unsigned i = 0; i < 4; ++i) result.history[i] = axi[0] + i * history;
            result.historyTransfers = history;
            result.copyTransfers = SplitCopyTransfers;
        }
        if (!result.valid()) return false;
        out = result;
        return true;
    }
    if (used == Geometry::Balanced) {
        // Each DMA controller writes one lane to AHB and one to AXI SRAM: lanes 0 and 2 are DMA1,
        // 1 and 3 DMA2 (capture_lanes.hpp). Lanes 0 and 1 share the slot's part of bank 0, lanes 2
        // and 3 take the same size at the start of the slot's half of banks 2 and 3, so the slots
        // stay inside the standard slots and never overlap a slot of the other geometry.
        const uint32_t laneBytes = BankBytes / count / 2;
        const uint32_t ahbOffset = slot * (BankBytes / count) / 2, axiOffset = slot * (BankBytes / 2) / 2;
        result.lanes[0] = ahb + ahbOffset;
        result.lanes[1] = ahb + ahbOffset + laneBytes / 2;
        result.lanes[2] = bank(2) + axiOffset;
        result.lanes[3] = bank(3) + axiOffset;
        result.laneTransfers = result.segmentTransfers = laneBytes / 2;
        if (!result.valid()) return false;
        out = result;
        return true;
    }
    const uint32_t laneBytes = BankBytes / count;
    uint32_t lane = laneBytes / (result.words() ? 2 : 1);
    // One segment per lane needs no buffer switching; take it while the ring still holds the
    // record plus the full-speed headroom, so the plan never has to shorten the record.
    if (depth + FullSpeedHeadroom <= 4 * MaximumSegment) lane = std::min(lane, MaximumSegment);
    lanes(result, bank, slot * laneBytes / 2);
    result.laneTransfers = lane;
    result.segmentTransfers = std::min(lane, MaximumSegment);
    if (!result.valid() || result.ringPoints() < depth) return false;
    out = result;
    return true;
}
bool AcquisitionArena::fullRate(const CaptureBuffer &buffer) {
    if (buffer.format == SampleFormat::Bytes) return true;
    // The planning threshold (see the declaration for what it does not guarantee): a lane in AHB
    // SRAM on each controller (lanes 0 and 2 are DMA1, 1 and 3 DMA2).
    const uintptr_t begin = reinterpret_cast<uintptr_t>(ahb), end = begin + BankBytes;
    const auto inAhb = [&](unsigned lane) {
        const uintptr_t first = reinterpret_cast<uintptr_t>(buffer.lanes[lane]);
        return first >= begin && first + buffer.wordsPerLane() * 2 <= end;
    };
    return (inAhb(0) || inAhb(2)) && (inAhb(1) || inAhb(3));
}
bool AcquisitionArena::trial(bool ch1, bool ch2, CaptureBuffer &out) {
    const unsigned channels = RecordBudget::channels(ch1, ch2);
    if (!channels) return false;
    CaptureBuffer result;
    result.format = channels == 2 ? SampleFormat::Words : SampleFormat::Bytes;
    result.byteChannel = ch2 && !ch1 ? Channel::Ch2 : Channel::Ch1;
    result.laneTransfers = result.segmentTransfers = TrialBytes / 4 / (channels == 2 ? 2 : 1);
    for (unsigned i = 0; i < 4; ++i) result.lanes[i] = trialArea + i * (TrialBytes / 8);
    result.region = Trial;
    if (!result.valid()) return false;
    out = result;
    return true;
}
bool AcquisitionArena::roll(CaptureBuffer &out, bool interleaved) {
    CaptureBuffer result;
    if (interleaved) result.format = SampleFormat::InterleavedWords;
    ahbQuarters(result);
    result.region = RollCapture;
    out = result;
    return result.valid();
}
bool AcquisitionArena::freeSlot(uint32_t budget, unsigned &slot, Geometry geometry) const {
    if (busy_ & Roll) return false;
    if (slots(budget, geometry) == 1) {
        if (busy_ & Slots) return false;
        slot = 0;
        return true;
    }
    for (unsigned candidate = 0; candidate < 2; ++candidate)
        if (!(busy_ & (candidate ? SlotB : SlotA))) { slot = candidate; return true; }
    return false;
}
bool AcquisitionArena::claim(uint8_t region) {
    if (!region || (busy_ & region) || ((region & Slots) && (busy_ & Roll)) || ((region & Roll) && (busy_ & Slots)))
        return false;
    busy_ |= region;
    return true;
}
} // namespace openosc::scope
