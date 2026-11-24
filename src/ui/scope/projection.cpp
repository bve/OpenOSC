#include "openosc/ui/scope/source.hpp"
#include "openosc/ui/scope/column_cache.hpp"
#include <algorithm>

namespace openosc::plot {
namespace {
struct Bucket {
    int32_t first = 0, last = 0, low = 0, high = 0;
    int64_t sum = 0;
    uint32_t lowAt = 0, highAt = 0, count = 0;
    __attribute__((always_inline)) void push(int32_t value) {
        if (!count)
            first = low = high = value;
        if (value < low) {
            low = value;
            lowAt = count;
        }
        if (value > high) {
            high = value;
            highAt = count;
        }
        last = value;
        sum += value;
        ++count;
    }
    // An empty bucket given a whole column at once: the same fields push() would leave.
    void take(const scope::ColumnStats &c) {
        if (!c.count) return;
        first = c.first * 256; last = c.last * 256; low = c.low * 256; high = c.high * 256;
        sum = int64_t(c.sum) * 256; lowAt = c.lowAt; highAt = c.highAt; count = c.count;
    }
};
// Reduction::Adaptive: each column's average, in pixels, between the column pass and adapt(), within
// one projectTime(). Scratch in DTCM .bss (the linker keeps the stack's 48 KiB beside it), not in
// Storage: the AXI SRAM that shares the capture arena has no room left.
int16_t means[3][MaximumWidth];
} // namespace
bool Model::projectTime(const scope::FrameView &frame, bool fresh, const ColumnCache *cache) {
    Source source(frame, options_, statistics_);
    // Columns reduced ahead (ColumnCache, while the display waited) for this frame and window.
    const unsigned ready = cache ? cache->ready(frame.metadata(), viewport_) : 0;
    for (unsigned c = 0; c < 3; ++c) {
        auto &trace = storage_.traces[c];
        trace.enabled = source.enabled(c);
        trace.clippedTop = trace.clippedBottom = false;
        trace.adcClipped = false;
        trace.zero = axes_[c < 2 ? c : 0].origin;
    }
    const bool aggregate = viewport_.aggregates();
    // A Roll record keeps its envelope: Adaptive decides from the whole screen's noise, so a change of
    // that decision would redraw all of the history in the other form.
    const bool adaptive = aggregate && options_.averaging <= 1 && options_.reduction == Reduction::Adaptive &&
                          !frame.metadata().hasRollPhase();
    int32_t excursion[2] = {};
    auto &screen = storage_.screen;
    screen.count[0] = screen.count[1] = 0;
    screen.valid[0] = screen.valid[1] = false;
    screen.first = UINT32_MAX; screen.last = 0;
    screen.minimum[0] = screen.minimum[1] = 127; screen.maximum[0] = screen.maximum[1] = -128;
    screen.sequence = frame.metadata().sequence; ++screen.projection;
    scope::BandEstimate bands[2];
    for (unsigned x = 0; x < viewport_.width; ++x) {
        Bucket bucket[3];
        if (aggregate) {
            uint32_t first, last;
            viewport_.column(x, frame.metadata().samples, first, last);
            if (first < last) { screen.first = std::min(screen.first, first); screen.last = std::max(screen.last, last); }
            if (options_.math == Math::Off) {
                // Plain traces need neither the math converter nor a three-channel dispatch
                // for every point. Reduce each visible channel with the frame's sequential
                // reader; all points and chronological extrema are still retained. Two channels
                // in one word are reduced together, each word read once.
                if (source.enabled(0) && source.enabled(1) && frame.pairs()) {
                    scope::ColumnStats columns[2];
                    if (x < ready) { columns[0] = cache->columns(x)[0]; columns[1] = cache->columns(x)[1]; }
                    else if (!ColumnCache::reduce(frame, first, last, columns)) return false;
                    bucket[0].take(columns[0]); bucket[1].take(columns[1]);
                    statistics_.sourceReads += 2 * (last - first);
                } else for (unsigned c = 0; c < 2; ++c) if (source.enabled(c)) {
                    auto reader = frame.cursor(scope::Channel(c), first);
                    scope::ColumnStats column;
                    if (reader.column(last - first, column) != last - first) return false;
                    bucket[c].take(column);
                    statistics_.sourceReads += last - first;
                }
            } else {
                for (uint32_t i = first; i < last; ++i) {
                    int32_t values[3];
                    if (!source.sample(i, values))
                        return false;
                    for (unsigned c = 0; c < 3; ++c)
                        if (source.enabled(c))
                            bucket[c].push(values[c]);
                }
            }
        } else {
            int32_t values[3];
            if (source.interpolate(viewport_.position(x), values))
                for (unsigned c = 0; c < 3; ++c)
                    if (source.enabled(c))
                        bucket[c].push(values[c]);
        }
        if (aggregate) // One record sample per column, the extremes of all of them.
            for (unsigned c = 0; c < 2; ++c) if (source.enabled(c) && bucket[c].count) {
                const auto &b = bucket[c];
                if (adaptive) excursion[c] = std::max(excursion[c], (b.high - b.low - std::abs(b.last - b.first)) / 256);
                const auto n = screen.count[c]++;
                const auto low = int16_t(bucket[c].low / 256), high = int16_t(bucket[c].high / 256);
                screen.codes[c][n] = int8_t(bucket[c].first / 256);
                screen.spans[c][n] = uint8_t(high - low);
                screen.minimum[c] = std::min(screen.minimum[c], low);
                screen.maximum[c] = std::max(screen.maximum[c], high);
                bands[c].add(low, high);
            }
        for (unsigned c = 0; c < 3; ++c) {
            auto &out = storage_.traces[c].columns[x];
            out = {};
            if (!source.enabled(c) || !bucket[c].count) {
                storage_.averageValid[c][x] = false;
                continue;
            }
            const auto &b = bucket[c];
            const auto &axis = axes_[c < 2 ? c : 0];
            if (options_.averaging > 1) {
                const int32_t value = b.sum / b.count;
                auto &average = storage_.averages[c][x];
                if (!storage_.averageValid[c][x])
                    average = value;
                else if (fresh)
                    average += (value - average) / options_.averaging;
                storage_.averageValid[c][x] = true;
                out.first = out.a = out.b = out.last = axis.pixel(average);
            } else if (options_.reduction==Reduction::Mean) {
                out.first=out.a=out.b=out.last=axis.pixel(b.sum/b.count);
            } else {
                if (adaptive) means[c][x] = axis.pixel(b.sum / b.count);
                out = {axis.pixel(b.first), axis.pixel(b.lowAt <= b.highAt ? b.low : b.high),
                       axis.pixel(b.lowAt <= b.highAt ? b.high : b.low), axis.pixel(b.last)};
            }
            auto &trace = storage_.traces[c];
            if (c < 2)
                trace.adcClipped = trace.adcClipped || b.low <= -32768 || b.high >= 32512;
            trace.clippedTop = trace.clippedTop || std::min(out.a, out.b) < 0;
            trace.clippedBottom = trace.clippedBottom || std::max(out.a, out.b) >= viewport_.height;
            ++statistics_.columns;
        }
    }
    // Frame averaging draws each column's mean, averaged over frames, whatever Samples says.
    for (unsigned c = 0; c < 3; ++c)
        storage_.traces[c].mean = options_.averaging > 1 || options_.reduction == Reduction::Mean;
    if (adaptive) adapt(excursion);
    screenSeries(frame, aggregate, source, bands);
    for (unsigned c = 0; c < 2; ++c)
        if (options_.peakHold && storage_.traces[c].enabled) {
            auto &peak = storage_.peaks[c];
            const auto &trace = storage_.traces[c];
            if (!peak.enabled)
                peak = trace;
            else
                for (unsigned x = 0; x < viewport_.width; ++x) {
                    const auto &p = trace.columns[x];
                    auto &b = peak.columns[x];
                    if (p.first == Missing)
                        continue;
                    if (b.first == Missing) {
                        b = p;
                        continue;
                    }
                    const auto low = std::min({b.a, b.b, p.a, p.b}), high = std::max({b.a, b.b, p.a, p.b});
                    b.a = low;
                    b.b = high;
                }
            peak.enabled = true;
        }
    xyCount_ = 0;
    return true;
}
void Model::adapt(const int32_t (&excursion)[2]) {
    const auto &screen = storage_.screen;
    bool mean[3] = {};
    for (unsigned c = 0; c < 2; ++c) {
        const unsigned n = screen.count[c];
        if (!storage_.traces[c].enabled || !n) continue;
        uint8_t spans[MaximumWidth];
        std::copy(screen.spans[c], screen.spans[c] + n, spans);
        std::nth_element(spans, spans + n / 2, spans + n);
        const int noise = spans[n / 2], band = screen.maximum[c] - screen.minimum[c];
        // Columns holding the whole swing (a fast signal, or noise alone) or one with a narrow
        // excursion beyond its noise (a pulse an average would hide) keep their extremes.
        mean[c] = 2 * noise <= band && excursion[c] <= 2 * noise + 4;
    }
    mean[2] = mean[0] && mean[1]; // Math: averaged only where both of its channels are.
    for (unsigned c = 0; c < 3; ++c) {
        auto &trace = storage_.traces[c];
        trace.mean = mean[c];
        if (!mean[c] || !trace.enabled) continue;
        trace.clippedTop = trace.clippedBottom = false;
        for (unsigned x = 0; x < viewport_.width; ++x) {
            auto &out = trace.columns[x];
            if (out.first == Missing) continue;
            out.first = out.a = out.b = out.last = means[c][x];
            trace.clippedTop = trace.clippedTop || out.a < 0;
            trace.clippedBottom = trace.clippedBottom || out.a >= viewport_.height;
        }
    }
}
// Completes the screen series: an aggregated window keeps its per-column samples; a window of
// fewer points than columns takes each of its record points (at most MaximumWidth) as they are.
void Model::screenSeries(const scope::FrameView &frame, bool aggregate, const Source &source, scope::BandEstimate (&bands)[2]) {
    auto &screen = storage_.screen;
    const auto &m = frame.metadata();
    const double rate = m.samplesPerSecond.value();
    screen.aggregated = aggregate;
    if (aggregate) {
        screen.samplesPerSecond = rate * viewport_.width * 65536.0 / double(viewport_.spanQ16 + 65536);
    } else {
        const int64_t a = std::max<int64_t>(0, -Viewport::floorSample(-viewport_.startQ16));
        const int64_t b = std::min<int64_t>(m.samples, Viewport::floorSample(viewport_.startQ16 + int64_t(viewport_.spanQ16)) + 1);
        screen.first = uint32_t(std::min<int64_t>(a, m.samples)); screen.last = uint32_t(std::max<int64_t>(a, b));
        screen.samplesPerSecond = rate;
        for (unsigned c = 0; c < 2; ++c) {
            if (!source.enabled(c)) continue;
            for (uint32_t i = screen.first; i < screen.last && screen.count[c] < MaximumWidth; ++i) {
                int16_t code;
                if (!frame.signedCode(scope::Channel(c), i, code)) break;
                screen.codes[c][screen.count[c]++] = int8_t(code);
                screen.minimum[c] = std::min(screen.minimum[c], code);
                screen.maximum[c] = std::max(screen.maximum[c], code);
                bands[c].add(code, code);
            }
        }
    }
    if (screen.first >= screen.last) screen.first = screen.last = 0;
    for (unsigned c = 0; c < 2; ++c) {
        screen.valid[c] = source.enabled(c) && screen.count[c] > 0;
        screen.band[c] = screen.valid[c] ? bands[c].band() : scope::SignalBand{};
    }
}
} // namespace openosc::plot
