#pragma once
#include "openosc/acquisition/frame.hpp"
#include "openosc/ui/scope/options.hpp"

namespace openosc::plot {
struct Viewport {
    // A held view at the next capture's time/div (Options::heldTimebaseNs) is not bounded by
    // FrameMetadata::valid, which bounds only the record's own: its span stops here, so
    // span * (width - 1) stays in int64. 180 MS/s at 20 s/div is below it.
    static constexpr uint64_t MaximumHeldSpanQ16 = uint64_t(1) << 53;
    int64_t startQ16 = 0;
    uint64_t spanQ16 = 0;
    uint16_t width = 0, height = 0;
    // A triggered record is placed by its own pretrigger: the trigger lies as far into the window as
    // it lies in the record (triggerIndex / (samples - 1)), so a window no longer than the record
    // stays within it. That fraction is the position the capture was made with; neither a saved view
    // (theme.json) nor the next RUN's request can move it. A shifted trigger (FrameMetadata::triggerShift:
    // no edge the ring held the record around) is placed in a trigger-locked Time/SpectrumOverlay view
    // as the record cut at its pretrigger point would be (panned and zoomed out too): its window
    // reaches past the record by the shift. Unlocked, X-Y and FFT views keep the real record origin.
    // `phaseQ16` (TriggerPhase::before): how far before triggerIndex the drawn curve crosses the
    // level. A phased() window is placed by that crossing instead of the index; any other ignores it.
    bool calculate(const scope::FrameMetadata &frame, const Options &options, unsigned w, unsigned h, uint32_t phaseQ16 = 0);
    // The span `options` asks for on `frame` before any bound (calculate then bounds a zoomed-out one
    // by the record): ten divisions at the shown time/div and the record's actual sample rate, times
    // zoomOut / zoom; at least one sample interval. 0 for a frame calculate refuses.
    static uint64_t requestedSpanQ16(const scope::FrameMetadata &frame, const Options &options);
    // A trigger-locked Time or Y-T+FFT window that draws the curve between its points (fewer points
    // than columns): the only one a crossing between points places (an aggregating window keeps the
    // index: a fraction of a point moves its columns by less than one). After calculate().
    bool phased(const scope::FrameMetadata &frame, const Options &options) const;
    int64_t position(unsigned x) const;
    int sampleX(uint32_t index) const { return positionX(int64_t(index) * 65536); }
    int positionX(int64_t positionQ16) const; // The x of a record position, clamped to +-4096.
    // The crossing's distance before triggerIndex this window is placed by (0: by the index). The
    // marker subtracts it too where the zoom-out clamp then moved the window: the crossing stays there.
    uint16_t phaseQ16 = 0; // Below one point.
    // The Roll grid's remainder at the window's start. A negative value marks an ordinary window.
    int16_t gridRemainder = -1;
    // Every record point lands in a column (the window spans at least a point per column).
    bool aggregates() const { return spanQ16 >= uint64_t(width - 1) * 65536; }
    // Column x's record points [first, last) of a record of `samples` (aggregating windows).
    void column(unsigned x, uint32_t samples, uint32_t &first, uint32_t &last) const;
    static int64_t floorSample(int64_t positionQ16) { // The record point at or before a Q16 position.
        return positionQ16 >= 0 ? positionQ16 / 65536 : -((-positionQ16 + 65535) / 65536);
    }
    bool operator==(const Viewport &o) const {
        return startQ16 == o.startQ16 && spanQ16 == o.spanQ16 && width == o.width && height == o.height && phaseQ16 == o.phaseQ16 &&
               gridRemainder == o.gridRemainder;
    }
private:
    // An aggregating Roll window on the grid of its continuous history (FrameMetadata::rollPhase).
    void rollGrid(const scope::FrameMetadata &m);
    // The window's Q16 length before the trigger: spanQ16 * point / (samples - 1), exact (floor)
    // without the 64-bit product, as quotient and remainder. The point is the trigger's index, or
    // with `anchored` (a locked time view) the record's pretrigger point (index - triggerShift).
    uint64_t pretrigger(const scope::FrameMetadata &m, bool anchored) const;
    // The same length for any record point: it lies as far into the window as into the record.
    uint64_t place(const scope::FrameMetadata &m, uint32_t point) const;
};
struct Axis {
    int32_t zeroQ8 = 0, codesPerDivisionQ8 = 8192;
    int16_t origin = 0;
    int16_t basePermille = 500; // Where the analog zero of this record puts 0 V, without the digital offset.
    // Pixels per Q8 code in Q32: exact to well under a pixel at any V/div a held record is shown at
    // (issue #93: up to 500 times its own, 20 mV to 10 V; Q16 lost a quarter of the scale there).
    int64_t factor = 0;
    bool configure(const scope::ChannelMetadata &metadata, const ChannelOptions &options, unsigned height);
    // Whether a held record is drawn at the next capture's V/div (ChannelOptions::heldMillivoltsAtTip): a
    // calibrated record whose codes can be drawn at it; otherwise at its own.
    static bool held(const scope::ChannelMetadata &metadata, const ChannelOptions &options);
    // The record's millivolts per division (at its input, before its probe) as it is drawn: its own,
    // or the held V/div at the tip through the record's own probe (its codes stay what they are).
    static double millivolts(const scope::ChannelMetadata &metadata, const ChannelOptions &options);
    // A record's basePermille: a calibrated record's codes are compensated for its analog zero, so
    // its line is placed here; raw codes already carry it (500). The digital position that centres
    // this record's zero is then 1000 - basePermille.
    static int16_t base(const scope::ChannelMetadata &metadata);
    int16_t pixel(int32_t codeQ8) const;
    double code(int pixel) const;
};
} // namespace openosc::plot
