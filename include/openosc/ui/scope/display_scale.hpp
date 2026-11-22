#pragma once
#include "openosc/ui/scope/viewport.hpp"
#include "openosc/data/decimal.hpp"
#include <cstdio>

namespace openosc::plot {
// What one division of the plot stands for: the shown record's own metadata, through its probe,
// the digital gain and the zoom. Every label of a plotted trace reads its scale from here.
struct ChannelScale {
    double perDivision = 0;
    const char *unit = "V";
};
class DisplayScale {
public:
    static ChannelScale channel(const scope::ChannelMetadata &metadata, const ChannelOptions &options) {
        const double divisions = metadata.calibrated ? metadata.millivoltsPerDivision / 1000.0 * metadata.probeNumerator / metadata.probeDenominator : 32;
        return {divisions * 100 / options.gainPercent, metadata.calibrated ? "V" : "cnt"};
    }
    static double secondsPerDivision(const Viewport &view, const scope::FrameMetadata &metadata) {
        return view.spanQ16 / 65536.0 / metadata.samplesPerSecond.value() / 10;
    }
    static const char *coupling(const scope::ChannelMetadata &metadata) {
        return metadata.coupling == scope::Coupling::Ac ? "AC" : "DC";
    }
    static void probe(const scope::ChannelMetadata &metadata, char *output, size_t capacity) {
        char number[16] = "?"; // The firmware's printf has no floating point.
        Decimal::fraction(uint64_t(metadata.probeNumerator) * 1000 / metadata.probeDenominator, 1000, number, sizeof number, 3);
        std::snprintf(output, capacity, "x%s", number);
    }
    // Whether the next capture's V/div (probe included) differs from the record's own.
    static bool voltsPending(const scope::FrameMetadata &record, const scope::FrameMetadata &next, unsigned c) {
        const auto &a = record.channels[c], &b = next.channels[c];
        return a.millivoltsPerDivision != b.millivoltsPerDivision || a.probeNumerator != b.probeNumerator ||
               a.probeDenominator != b.probeDenominator;
    }
    // Whether the next capture's time/div differs from the one the record is shown at: a held record
    // already follows the next time/div (Options::heldTimebaseNs, issue #7), so that one is not pending.
    static bool timePending(const scope::FrameMetadata &record, const Options &options, const scope::FrameMetadata &next) {
        return (options.heldTimebaseNs ? options.heldTimebaseNs : record.timebaseNs) != next.timebaseNs;
    }
};
} // namespace openosc::plot
