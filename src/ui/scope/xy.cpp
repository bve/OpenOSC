#include "openosc/ui/scope/source.hpp"
#include <algorithm>

namespace openosc::plot {
bool Model::projectXY(const scope::FrameView &frame) {
    Source source(frame, options_, statistics_);
    xyCount_ = 0;
    for (auto &trace : storage_.traces)
        trace.enabled = false;
    if (!source.enabled(0) || !source.enabled(1))
        return true;
    const unsigned count =
        std::min<unsigned>(MaximumXYPoints, std::max<unsigned>(2, unsigned(viewport_.spanQ16 / 65536) + 1));
    for (unsigned i = 0; i < count; ++i) {
        int32_t values[3];
        auto &point = storage_.xy[i];
        point = {};
        if (!source.interpolate(viewport_.startQ16 + int64_t(viewport_.spanQ16 * i / (count - 1)), values))
            continue;
        const int delta = axes_[0].origin - axes_[0].pixel(values[0]);
        // A higher line on the Y-T screen is a higher CH1 voltage: on the X axis, further right.
        const int64_t x = int64_t(viewport_.width - 1) * (1500 - axes_[0].basePermille - options_.channels[0].positionPermille) / 1000 +
                          int64_t(delta) * (viewport_.width - 1) * 8 / ((viewport_.height - 1) * 10);
        point.x = int16_t(std::clamp<int64_t>(x, -4096, 4096));
        point.y = axes_[1].pixel(values[1]);
    }
    xyCount_ = count;
    return true;
}
} // namespace openosc::plot
