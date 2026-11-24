#pragma once
#include "openosc/settings/run_mode.hpp"
#include "openosc/acquisition/frame.hpp"
#include "openosc/acquisition/record_depths.hpp"
#include "openosc/acquisition/arena.hpp"
#include <algorithm>
#include <numeric>

namespace openosc::scope {
struct SamplePlan {
    // At the rate cap the trigger freeze lands a few microseconds late; the ring keeps
    // this many points spare so the late DMA writes cannot overwrite the record's start.
    // Rings of half the arena or less are deeper than their record, so only a ring the
    // record fills (256K per channel, or the 512K budget) shortens it at full speed.
    static constexpr uint32_t FullSpeedHeadroom = AcquisitionArena::FullSpeedHeadroom;
    // A split ring (CaptureBuffer::split) has nothing spare past its capacity, so its headroom is the
    // time the freeze may land late at the capped rate: the comparator and the post-trigger timer are
    // serviced at priority 3, above USB and DMA. Each record's actual lateness is traced at the freeze.
    static constexpr uint32_t SplitStopNs = 18000;
    // In points at the plan's point rate (pointsPerTransfer x the transfer rate): the full-speed
    // headroom is FullSpeedHeadroom transfers, the split one SplitStopNs of points.
    static uint32_t headroom(const CaptureBuffer &buffer, uint32_t maximumPointRate) {
        if (!buffer.split()) return FullSpeedHeadroom * buffer.pointsPerTransfer();
        return uint32_t((uint64_t(maximumPointRate) * SplitStopNs + 999999999) / 1000000000);
    }
    uint32_t count = 0;
    Rational rate;
    // `depth` points per channel in a capture ring that surely holds `ringPoints`. `maximumRate` is
    // the DMA transfer rate; each transfer carries `pointsPerTransfer` points (InterleavedWords: 2),
    // so the points come at up to pointsPerTransfer x maximumRate and in groups of 4 x that (one
    // transfer per lane), and `headroom` is in points.
    static bool calculate(uint64_t timebaseNs, uint32_t depth, uint32_t maximumRate, uint32_t ringPoints, SamplePlan &out,
                          uint32_t headroom = FullSpeedHeadroom, uint32_t pointsPerTransfer = 1, uint32_t windowPoints = 0) {
        const uint32_t granule = 4 * pointsPerTransfer;
        if (timebaseNs < 10 || timebaseNs > 20000000000ULL || !RecordDepth::valid(depth) || ringPoints < depth ||
            !maximumRate || maximumRate > MaximumSampleRate || (pointsPerTransfer != 1 && pointsPerTransfer != 2) ||
            depth % granule) return false;
        const uint64_t pointRate = uint64_t(maximumRate) * pointsPerTransfer;
        // The record still uses the requested depth. A separate window budget keeps the sample
        // rate independent of added record memory, leaving the extra samples for panning and zoom-out.
        // A zero window budget puts as much of the record on screen as the rate cap allows.
        SamplePlan plan;
        plan.count = depth;
        const uint32_t visible = windowPoints ? std::clamp(windowPoints, granule, depth) : depth;
        uint64_t numerator = uint64_t(visible - 1) * 100000000;
        uint64_t denominator = timebaseNs;
        const bool limited = numerator > pointRate * denominator;
        if (limited) {
            numerator = pointRate;
            denominator = 1;
        }
        // A record longer than the screen still needs ring headroom below the rate cap:
        // the DMA must not overwrite its start while the trigger stop is being serviced.
        if ((limited || visible < depth) && ringPoints - plan.count < headroom)
            plan.count = ringPoints > headroom + granule ? (ringPoints - headroom) & ~(granule - 1) : granule;
        const uint64_t common = std::gcd(numerator,denominator);
        numerator/=common; denominator/=common;
        if (numerator>UINT32_MAX || denominator>UINT32_MAX) return false;
        plan.rate={uint32_t(numerator),uint32_t(denominator)};
        if (plan.rate.value()<2.0*pointRate/(65536.0*16384)) return false;
        out=plan;
        return true;
    }
};
}
