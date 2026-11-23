#include "openosc/ui/scope/column_cache.hpp"

namespace openosc::plot {
bool ColumnCache::applies(const scope::FrameView &frame, const Options &options, const Viewport &viewport) {
    const auto *c = frame.metadata().channels;
    return (options.mode == Mode::Time || options.mode == Mode::SpectrumOverlay) && options.math == Math::Off &&
           viewport.aggregates() && frame.pairs() && c[0].enabled && c[1].enabled &&
           options.drawn(0) && options.drawn(1);
}
bool ColumnCache::reduce(const scope::FrameView &frame, uint32_t first, uint32_t last, scope::ColumnStats (&out)[2]) {
    auto reader = frame.pairCursor(first);
    return reader.column(last - first, out) == last - first;
}
bool ColumnCache::prepare(const scope::FrameView &frame, const Options &options, unsigned width, unsigned height,
                          uint32_t points) {
    Viewport viewport;
    if (!frame.valid() || !viewport.calculate(frame.metadata(), options, width, height) ||
        !applies(frame, options, viewport)) { clear(); return true; }
    const auto &m = frame.metadata();
    if (!same(m, viewport)) {
        sequence_ = m.sequence; timestamp_ = m.timestampUs; trigger_ = m.triggerIndex;
        viewport_ = viewport; done_ = 0; valid_ = true;
    }
    uint32_t read = 0;
    while (done_ < viewport_.width && read < points) {
        uint32_t first, last;
        viewport_.column(done_, m.samples, first, last);
        if (!reduce(frame, first, last, stats_[done_])) { clear(); return true; }
        read += last - first;
        ++done_;
    }
    return done_ == viewport_.width;
}
} // namespace openosc::plot
