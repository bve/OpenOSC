#pragma once
#include "openosc/acquisition/frame.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc::scope {
// Two channels are captured as one 16-bit GPIOD word per point; one channel as the byte of
// its ADC lane (CH1 low, CH2 high), so a single channel gets every byte of the record budget.
// InterleavedWords (ZT706S): CH1 through both ADCs in opposite clock phase, one GPIOD word per DMA
// transfer holding two of its points (Layout::InterleavedCh1): the same transfers and geometries as
// two channels' words, twice the points.
enum class SampleFormat : uint8_t { Words, Bytes, InterleavedWords };
// Where one capture writes: DMA lane i stores transfers 4n+i in lanes[i], a ring of laneTransfers
// DMA transfers (words or bytes) split into equal segments that each fit the 16-bit NDTR. Everything
// about the DMA (NDTR, lanes, segments, the history schedule, progress and event cursors) counts
// transfers; the record (FrameMetadata, sample plan, trigger gate, rate) counts points, and
// pointsPerTransfer() is the one conversion between the two.
struct CaptureBuffer {
    uint16_t *lanes[4]{};
    uint32_t laneTransfers = 0, segmentTransfers = 0;
    SampleFormat format = SampleFormat::Words;
    Channel byteChannel = Channel::Ch1; // The channel a byte capture reads.
    uint8_t region = 0;                 // AcquisitionArena region bits owned while it is written.
    // Split (two channels at the full rate): the lanes write only AHB SRAM, ringing through their
    // laneTransfers words, and the MDMA copies each block of copyTransfers words to the lane's history
    // of historyTransfers words in AXI SRAM just before the lane comes back to it (HistorySchedule).
    uint16_t *history[4]{};
    uint32_t historyTransfers = 0, copyTransfers = 0;
    bool split() const { return historyTransfers != 0; }
    bool words() const { return format != SampleFormat::Bytes; } // 16-bit transfers.
    uint32_t pointsPerTransfer() const { return format == SampleFormat::InterleavedWords ? 2 : 1; }
    // The ring in transfers (all lanes); a split ring spans each lane's capture ring and history.
    uint32_t transferRing() const { return (laneTransfers + historyTransfers) * 4; }
    // The view's ring of points.
    uint32_t ringPoints() const { return transferRing() * pointsPerTransfer(); }
    // Transfers (points) a frozen record surely holds: a split ring has blocks copied ahead in both memories.
    uint32_t capacityTransfers() const;
    uint32_t capacityPoints() const { return capacityTransfers() * pointsPerTransfer(); }
    uint32_t segments() const { return segmentTransfers ? laneTransfers / segmentTransfers : 0; }
    uint32_t segmentBytes() const { return segmentTransfers * (words() ? 2 : 1); }
    size_t wordsPerLane() const { return words() ? laneTransfers : (laneTransfers + 1) / 2; }
    Layout layout() const {
        return format == SampleFormat::Words ? Layout::FourBanks
             : format == SampleFormat::Bytes ? Layout::FourByteBanks : Layout::InterleavedCh1;
    }
    // Where chronological segment `index` of a lane goes: the lane ring repeats every segments().
    uintptr_t segmentAddress(unsigned lane, uint64_t index) const {
        return reinterpret_cast<uintptr_t>(lanes[lane]) + uintptr_t(index % segments()) * segmentBytes();
    }
    bool valid() const;
};
// The DMA-reachable record memory and who owns which part of it. Bank 0 is AHB SRAM, banks 1-3
// one AXI SRAM block: 4 x 128 KiB, exactly the largest record budget, so a record is never copied.
// A budget of half the arena or less gets two slots (each bank split in halves): a new capture
// never writes the frame on show or in HOLD. The 512 KiB budget has one slot covering everything.
// Two channels at the full rate take the compact geometry up to 128K (one slot, all of bank 0) and the
// split one above it (one slot, the whole arena: the lanes ring through bank 0, the MDMA keeps the older
// words in banks 1-3), which keeps the points of the budget, 256K or 512K, the record asks for. A
// one-slot capture takes the memory of the frame on show only once that frame has been seen and nobody
// reads it.
// Roll takes the whole arena: its DMA ring in bank 0, the history in bank 1, snapshots in bank 2;
// bank 3, which no Roll part writes, holds the Scope's DMM trace (rollSpare).
// AutoSetup trials and zero calibration capture into a separate trial area, which is how a HOLD
// record survives an AutoSetup and comes back unchanged when it is cancelled or fails.
class AcquisitionArena {
public:
    static constexpr uint32_t BankBytes = 131072, Banks = 4, Bytes = BankBytes * Banks;
    static constexpr uint32_t TrialBytes = 16384;
    static constexpr uint32_t MaximumSegment = 32768; // Transfers; a power of two below the 16-bit NDTR limit.
    static constexpr uint32_t MaximumLaneTransfers = BankBytes; // One channel in the full-arena slot.
    static constexpr uint32_t FullSpeedHeadroom = 4096; // Points; see SamplePlan.
    static constexpr uint32_t RollRingPoints = 65536, RollWindow = 65536; // Words: 16384 per lane in bank 0.
    enum Region : uint8_t { SlotA = 1, SlotB = 2, RollCapture = 4, RollSnapshot = 8, Trial = 16 };
    static constexpr uint8_t Slots = SlotA | SlotB, Roll = RollCapture | RollSnapshot;
    // Standard: the four lanes in banks 0-3. Balanced (two channels only): two lanes in AHB and two
    // in AXI SRAM, one of each per DMA controller (planned at the full rate, see fullRate); only the
    // 64K budget below half the full rate takes it now. Split (two channels only, one slot): every lane
    // rings through a quarter of bank 0 and the MDMA keeps its older words in a quarter of banks 1-3,
    // so the capture DMA writes only AHB SRAM; whatever the budget, the ring is the whole arena and the
    // record holds the budget's points (at most HistorySchedule::capacity less the stop headroom). Compact
    // (two channels only, one slot, budgets up to one bank): every lane rings through a quarter of
    // bank 0 and nothing else, so no capture DMA write goes to AXI SRAM; its ring of 65536 points
    // per channel holds 32768 with room to spare, or 65536 less the full-speed headroom.
    enum class Geometry : uint8_t { Standard, Balanced, Split, Compact };
    static constexpr uint32_t SplitCopyTransfers = 128; // HistorySchedule block: 256 bytes of a lane.
    static unsigned slots(uint32_t budget, Geometry geometry = Geometry::Standard);
    // The geometry of a record: `fast` when its plan needs more than half the full rate. Two fast
    // channels take Compact up to 128K and Split above it (256K and 512K: Balanced held at most 131072
    // points per channel, half of the 512K budget). On the ZT703S at 280 MHz the 512K split ring kept
    // 258592 points per channel at 140 MS/s for 300 s with a clean stop (HIL 2026-11-11, candidate17
    // local1); 256K and the 200/320 MHz profiles are not measured on the device.
    static Geometry geometry(uint32_t budget, bool ch1, bool ch2, bool fast);
    // An interleaved CH1 record (always at the full word rate): every lane in AHB, as two fast channels.
    static Geometry interleavedGeometry(uint32_t budget) { return budget <= BankBytes ? Geometry::Compact : Geometry::Split; }
    // The capture layout of a record budget in one slot; false for an unlisted budget or no channel.
    // A balanced ring holds at most 131072 points per channel, a compact one 65536; the plan keeps
    // within it.
    // `interleaved`: CH1 alone as InterleavedWords (ch1 only): the budget's points in half as many
    // words, laid out as two channels' words of the same budget (Compact up to one bank, else Split).
    static bool record(uint32_t budget, bool ch1, bool ch2, unsigned slot, CaptureBuffer &out,
                       Geometry geometry = Geometry::Standard, bool interleaved = false);
    // Whether the plan may take the full rate (timer clock / 2) in this buffer: bytes, or two channels
    // with a lane in AHB on each DMA controller. This is a planning threshold, not a guarantee. Each
    // controller has one memory port for all its streams, and a word lane's FIFO has room for only
    // about 4 requests past its burst threshold (~114 ns at 140 MS/s), so a stalled write of any
    // stream on that controller can overrun the others (FE). On the ZT703S at 140 MS/s (HIL
    // 2026-11-10/11, build/hardware/record-depth-hil/v2/HIL_REPORT_V2.md):
    // - both word lanes of a controller in AXI: FE within seconds;
    // - Balanced (one AHB and one AXI lane per controller): FE on the AHB lanes 0/1 under USB data
    //   reads, and twice within the setup steps of a run with control traffic only (C7);
    // - all four word lanes in AHB, one channel's bytes: no FE in the runs made.
    // Two channels at half the rate are planned in the standard geometry.
    static bool fullRate(const CaptureBuffer &buffer);
    static bool trial(bool ch1, bool ch2, CaptureBuffer &out);
    static bool roll(CaptureBuffer &out, bool interleaved = false);
    static uint16_t *rollHistory();  // RollWindow words.
    static uint16_t *rollSnapshot(); // RollWindow words.
    static uint16_t *rollSpare();    // Bank 3: no capture writes it while Roll holds the arena; the DMM trace's readings live there.
    // A free slot for a budget, or false while the frame on show, HOLD or Roll hold the memory.
    bool freeSlot(uint32_t budget, unsigned &slot, Geometry geometry = Geometry::Standard) const;
    // Slots exclude Roll and each other; Roll parts exclude every slot; the trial area is apart.
    bool claim(uint8_t region);
    void release(uint8_t region) { busy_ &= uint8_t(~region); }
    uint8_t busy() const { return busy_; }
    static uint16_t *bank(unsigned index);
    static uint16_t *trialMemory();

private:
    uint8_t busy_ = 0; // Claimed and released by the foreground only.
};
} // namespace openosc::scope
