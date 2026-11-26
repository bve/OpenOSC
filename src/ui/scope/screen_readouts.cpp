#include "openosc/ui/scope/screen_readouts.hpp"

namespace openosc::plot {
// The columns resolve the signal when every column holding a large swing (its own envelope spans
// more than half of the window's range) shows that swing as one edge in the series: its sample
// (the column's first point) and the next column's lie on either side of the middle. A tone
// folding into the column rate, a burst or a lone narrow pulse (also in the first column) leaves
// unexplained wide columns: frequency, duty and pulse count would be the columns' sampling.
// The last column has no next sample: a large swing there may be a pulse the series cannot show,
// so the pulse count is unknown, while frequency and duty from the whole periods before it stand.
ScreenReadouts::Resolution ScreenReadouts::resolution(const ScreenSeries &series, unsigned c) {
    if (!series.aggregated) return {true, true}; // Every record point of the window is in the series.
    const int span = series.maximum[c] - series.minimum[c];
    if (span < 4) return {true, true};
    const int twiceMiddle = series.maximum[c] + series.minimum[c];
    const unsigned n = series.count[c];
    for (unsigned i = 0; i + 1 < n; ++i)
        if (2 * series.spans[c][i] > span &&
            (2 * series.codes[c][i] > twiceMiddle) == (2 * series.codes[c][i + 1] > twiceMiddle))
            return {false, false};
    return {true, !(n && 2 * series.spans[c][n - 1] > span)};
}
// Edges from the series are crossings between two of its samples, interpolated: each is off by at
// most half a sample with a sharp edge (one sample per column, the column's first point), however
// the edges fall on the columns - a signal locked to the column rate repeats the same error in every
// period, so averaging proves nothing. The frequency spans `frequencySpan` samples between its first
// and last rising crossing: at most one sample of it is wrong. A duty cycle's high time has two
// crossings: up to one sample of a period of P = rate / f samples, 100 / P percent.
void ScreenReadouts::bound(scope::Measurements &values, double samplesPerSecond) {
    using scope::Measurement;
    double hertz = 0;
    if (!values.value(Measurement::Frequency, hertz) || values.frequencySource() != scope::FrequencySource::Edges ||
        !(hertz > 0) || !(samplesPerSecond > 0))
        return;
    const double span = values.frequencySpan();
    if (span > 1) {
        values.bound(Measurement::Frequency, hertz / (span - 1));
        values.bound(Measurement::Period, 1 / (hertz * (span - 1)));
    }
    values.bound(Measurement::Duty, 100 * hertz / samplesPerSecond);
}
scope::ReadoutResult ScreenReadouts::calculate(const ScreenSeries &series, const scope::FrameMetadata &metadata,
                                               const scope::ReadoutSelection &selection, uint32_t now) {
    scope::ReadoutResult result;
    result.basis = scope::ReadoutBasis::Screen;
    result.timestampUs = metadata.timestampUs;
    result.captureRate(metadata.samplesPerSecond);
    result.sequence = series.sequence;
    result.first = series.first; result.points = series.last - series.first;
    result.finishedAt = now;
    for (unsigned c = 0; c < 2; ++c) {
        if (!series.valid[c] || !metadata.channels[c].enabled || !selection.needs(c)) continue;
        // Edges, mean and RMS from the series (its own extremes set the 30/50/70 % thresholds);
        // then Vpp/Min/Max replaced by the exact extremes of every point in the window, and the
        // robust levels by the band of the window's columns.
        auto summary = scope::Analyzer::summarize(series.codes[c], series.count[c], series.samplesPerSecond, selection.needsEdges(c));
        const auto resolved = resolution(series, c);
        if (!resolved.edges || (summary.hasFrequency && summary.frequencyHz * 4 > series.samplesPerSecond)) {
            summary.hasFrequency = summary.hasDuty = summary.hasPulses = false; // Not physical: unknown.
            summary.frequencyHz = summary.dutyPercent = summary.frequencySpan = 0;
            summary.frequencySource = scope::FrequencySource::None;
        }
        if (!resolved.count) summary.hasPulses = false;
        summary.minimum = series.minimum[c];
        summary.maximum = series.maximum[c];
        summary.hasRobust = series.band[c].valid;
        summary.robustMinimum = series.band[c].low; summary.robustMaximum = series.band[c].high;
        result.values[c] = scope::Measurements::calculate(summary, metadata.channels[c]);
        bound(result.values[c], series.samplesPerSecond);
        result.computed[c] = true;
    }
    result.valid = true;
    return result;
}
} // namespace openosc::plot
