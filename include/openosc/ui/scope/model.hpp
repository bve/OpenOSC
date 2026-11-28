#pragma once
#include <array>
#include "openosc/ui/scope/viewport.hpp"
#include "openosc/ui/scope/persistence.hpp"
#include "openosc/acquisition/analysis.hpp"

namespace openosc::plot {
class Source;
class ColumnCache;
// The screen's compact view of a time projection, for the fast readouts. Extrema are exact over
// every record point the window covers; the series holds one record sample per display column
// (its first point), or every point when the window shows fewer points than columns.
struct ScreenSeries {
    int8_t codes[2][MaximumWidth]; // ADC codes are signed 8-bit; the arena shares AXI with this.
    // With one sample per column, the span of that column's own envelope (high - low of all its
    // record points); `aggregated` is false when the series holds every point instead.
    uint8_t spans[2][MaximumWidth];
    bool aggregated = false;
    unsigned count[2]{};
    int16_t minimum[2]{}, maximum[2]{};
    // The robust levels (BandEstimate): units are the columns' envelopes, or the points when the
    // series holds every point; one or two spiking units set no level.
    scope::SignalBand band[2];
    uint32_t first = 0, last = 0; // Record indices [first, last) the window covers.
    double samplesPerSecond = 0;  // Of the series: the record rate divided by points per column.
    uint32_t sequence = 0, projection = 0;
    bool valid[2]{};
};
struct Storage {
    Trace traces[3], reference, peaks[2], fft;
    ScreenSeries screen;
    Point xy[MaximumXYPoints];
    int32_t averages[3][MaximumWidth];
    bool averageValid[3][MaximumWidth];
    Persistence persistence;
};
struct CursorReadout {
    double deltaSeconds = 0, reciprocalHz = 0, deltaCodes = 0, deltaVolts = 0;
    bool timeValid = false, reciprocalValid = false, codesValid = false, voltsValid = false;
    double deltaFrequencyHz=0;
    bool frequencyValid=false;
    double deltaDecibels=0;
    bool decibelsValid=false;
};
struct Statistics {
    uint32_t frames = 0, projections = 0, sourceReads = 0, columns = 0, drawBatches = 0, fftCalculations = 0;
};
class Model {
public:
    Model(Storage &storage, scope::SpectrumStorage &spectrum) : storage_(storage), spectrum_(spectrum) {}
    void reset();
    bool configure(const Options &options);
    // `cache`: columns reduced ahead for this frame (ColumnCache), or none.
    bool project(const scope::FrameView &frame, unsigned width, unsigned height, uint32_t now,
                 const ColumnCache *cache = nullptr);
    void trigger(scope::Channel channel, double codes); // In this record's codes; may lie off screen.
    bool cursorLines(const CursorOptions &cursors); // Only the overlay changes: nothing is re-projected.
    void cursorFocus(CursorLine line) { cursorFocus_ = line; }
    CursorLine cursorFocus() const { return cursorFocus_; }
    // The selected channel's trace is drawn over the other's (GitHub #81): a view choice, the
    // record, its channels and the projection stay as they are.
    void frontChannel(unsigned channel) { frontChannel_ = channel <= MathView::TraceIndex ? channel : 0; }
    unsigned frontChannel() const { return frontChannel_; }
    std::array<unsigned, 3> traceOrder() const {
        if (frontChannel_ == MathView::TraceIndex) return {0, 1, MathView::TraceIndex};
        return {MathView::TraceIndex, frontChannel_ ^ 1, frontChannel_};
    }
    double mathPerDivision() const;
    bool captureReference(unsigned channel);
    void clearReference() { reference_ = false; }
    void clearHistory();
    void clear();
    CursorReadout cursors() const;
    const Options &options() const { return options_; }
    const Viewport &viewport() const { return viewport_; }
    const scope::FrameMetadata &metadata() const { return metadata_; }
    const Axis &axis(unsigned channel) const { return axes_[channel <= MathView::TraceIndex ? channel : 0]; }
    const Storage &storage() const { return storage_; }
    const ScreenSeries &screen() const { return storage_.screen; }
    // Whether a trace's columns show an average now (HD, Adaptive's choice, or frame averaging,
    // which overrides Samples) rather than their extremes.
    bool effectiveMean(unsigned trace) const { return trace < 3 && storage_.traces[trace].mean; }
    Statistics &statistics() { return statistics_; }
    const Statistics &statistics() const { return statistics_; }
    bool needsFrame() const { return dirty_; }
    bool hasFrame() const { return valid_; }
    bool hasReference() const { return reference_; }
    unsigned xyCount() const { return xyCount_; }
    bool agePersistence(uint32_t now) { return storage_.persistence.advance(now, options_.persistenceMs); }
    // HOLD (GitHub #85): no record steps or ages a density plane, and the time held ages nothing after.
    void hold(bool held, uint32_t now);
    // Whether the record has a trigger position to mark: its trigger, or (untriggered: Auto's newest
    // points, an edge Noise reject refused, a forced record) its configured pretrigger point (#65).
    bool triggerPlaced() const { return metadata_.triggered || metadata_.pretriggerKnown; }
    // The trigger's place: the crossing the window is placed by (Viewport::phaseQ16), else its index;
    // untriggered, the record's pretrigger point.
    int triggerX() const {
        if (!metadata_.triggered && metadata_.pretriggerKnown) return viewport_.sampleX(metadata_.pretrigger);
        return viewport_.positionX(int64_t(metadata_.triggerIndex) * 65536 - viewport_.phaseQ16);
    }
    int triggerY() const { return axes_[unsigned(triggerChannel_)].pixel(int32_t(triggerLevel_ * 256)); }
    scope::Channel triggerChannel() const { return triggerChannel_; }
    const scope::SpectrumResult &spectrum() const { return spectrum_.result; }
    double spectrumFrequency(unsigned permille) const;

private:
    bool projectTime(const scope::FrameView &frame, bool fresh, const ColumnCache *cache);
    // Reduction::Adaptive: each trace's columns become their averages unless they hold a whole
    // swing or a narrow excursion (excursion: the largest high - low - |last - first| of a column).
    void adapt(const int32_t (&excursion)[2]);
    // bands: the robust estimate of each channel, from the columns when aggregated; completed here.
    void screenSeries(const scope::FrameView &frame, bool aggregate, const Source &source, scope::BandEstimate (&bands)[2]);
    bool projectXY(const scope::FrameView &frame);
    bool projectSpectrum(const scope::FrameView &frame, bool recalculate);
    void accumulate(uint32_t now);
    Storage &storage_;
    scope::SpectrumStorage &spectrum_;
    Options options_;
    Viewport viewport_;
    Axis axes_[3];
    scope::FrameMetadata metadata_;
    Statistics statistics_;
    unsigned xyCount_ = 0;
    bool spectrumDirty_ = true;
    double firstBin_=0,binSpan_=0;
    scope::Channel triggerChannel_ = scope::Channel::Ch1;
    double triggerLevel_ = 0;
    CursorLine cursorFocus_ = CursorLine::None;
    unsigned frontChannel_ = 0; // As the controller's first selection.
    bool valid_ = false, dirty_ = true, reference_ = false, held_ = false;
    uint32_t heldAt_ = 0;
    scope::TriggerEdge densityEdge_; // The last live triggered record's: a new definition clears the density.
};
Storage &applicationStorage();
scope::SpectrumStorage &applicationSpectrum();
ColumnCache &applicationColumns();
} // namespace openosc::plot
