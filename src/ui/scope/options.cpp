#include "openosc/ui/scope/options.hpp"
#include "openosc/acquisition/timebases.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

namespace openosc::plot {
bool MeterTraceOptions::valid() const {
    if (position < -PositionLimit || position > PositionLimit) return false;
    for (unsigned i = 0; i < MeterScale::Quantities; ++i)
        if (!MeterScale::valid(i, scales[i])) return false;
    return true;
}
bool Options::valid() const {
    if (unsigned(mode) > 3 || unsigned(style) > 2 || unsigned(grid) > 3 || unsigned(math) > 2 || !spectrum.valid() || !counter.valid() ||
        !meter.valid() ||
        unsigned(reduction)>2 || unsigned(interpolation)>2 || !zoom || !zoomOut ||
        zoom > MaximumZoom || panPpb < -PanFull || panPpb > PanFull ||
        (persistenceMs > 10000 && persistenceMs!=InfinitePersistence) ||
        !averaging || averaging > 64 || (averaging & (averaging - 1)) || gridBrightness > Brightness::Full ||
        (heldTimebaseNs && std::find(std::begin(scope::Timebases::Values), std::end(scope::Timebases::Values),
                                     heldTimebaseNs) == std::end(scope::Timebases::Values)))
        return false;
    for (const auto &c : channels)
        if (c.positionPermille < -1000 || c.positionPermille > 2000 || c.gainPercent < 1 ||
            c.gainPercent > 6400 || c.width < 1 || c.width > 3 ||
            !std::isfinite(c.heldMillivoltsAtTip) || c.heldMillivoltsAtTip < 0 || unsigned(c.grade) > 2 ||
            (c.gradeColor != ChannelOptions::InheritColor && c.gradeColor >= 16) ||
            c.brightness < Brightness::LowestTrace || c.brightness > Brightness::Full)
            return false;
    const auto &c = cursors;
    return c.xA <= 1000 && c.xB <= 1000 && c.yA <= 1000 && c.yB <= 1000 && c.channel <= CursorOptions::Meter;
}
void Options::restoreHeldView(const Options &saved) {
    zoom = saved.zoom; zoomOut = saved.zoomOut; panPpb = saved.panPpb; heldTimebaseNs = saved.heldTimebaseNs;
    centerHeldTrigger = saved.centerHeldTrigger;
    for (unsigned c = 0; c < 2; ++c) {
        channels[c].gainPercent = saved.channels[c].gainPercent;
        channels[c].positionPermille = saved.channels[c].positionPermille;
        channels[c].heldMillivoltsAtTip = saved.channels[c].heldMillivoltsAtTip;
    }
}
bool Options::sameTransform(const Options &b) const {
    if (mode != b.mode || zoom != b.zoom || zoomOut != b.zoomOut || panPpb != b.panPpb || heldTimebaseNs != b.heldTimebaseNs ||
        lockTrigger != b.lockTrigger || centerHeldTrigger != b.centerHeldTrigger ||
        averaging != b.averaging || math != b.math ||
        reduction != b.reduction || interpolation != b.interpolation || !spectrum.same(b.spectrum))
        return false;
    for (unsigned i = 0; i < 2; ++i) {
        const auto &x = channels[i];
        const auto &y = b.channels[i];
        if (x.positionPermille != y.positionPermille || x.gainPercent != y.gainPercent ||
            x.invert != y.invert || x.visible != y.visible || x.heldMillivoltsAtTip != y.heldMillivoltsAtTip ||
            x.graded() != y.graded())
            return false;
    }
    return true;
}
} // namespace openosc::plot
