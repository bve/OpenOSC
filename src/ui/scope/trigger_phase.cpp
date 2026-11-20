#include "openosc/ui/scope/trigger_phase.hpp"
#include "openosc/ui/scope/source.hpp"

namespace openosc::plot {
uint32_t TriggerPhase::before(const scope::FrameView &frame, const Options &options) {
    const auto &m = frame.metadata();
    const auto &edge = m.triggerEdge;
    const unsigned c = unsigned(edge.channel);
    if (!frame.valid() || !m.triggered || !edge.known || m.rolling || c > 1 || m.triggerIndex < 1 ||
        m.triggerIndex >= m.samples || !m.channels[c].enabled)
        return 0;
    // The view's curve of the trigger's source, shown or not (a hidden source still places the rest),
    // without math; its own statistics (the projection's counts stay the projection's).
    Options curve = options;
    curve.math = Math::Off;
    curve.channels[c].visible = true;
    Statistics statistics;
    Source source(frame, curve, statistics);
    const int64_t base = int64_t(m.triggerIndex - 1) << 16, level = int64_t(edge.level) * 256;
    bool ok = true;
    const auto starting = [&](uint32_t t) { // At base + t: on the side the edge starts from?
        int32_t values[3];
        if (!source.interpolate(base + t, values)) { ok = false; return false; }
        return edge.rising ? values[c] <= level : values[c] >= level;
    };
    if (!starting(0) || starting(65536) || !ok) return 0;
    uint32_t low = 0, high = 65536; // starting(low), !starting(high).
    for (unsigned step = 0; step < Steps && high - low > 1; ++step) {
        const uint32_t middle = (low + high) / 2;
        if (starting(middle)) low = middle; else high = middle;
        if (!ok) return 0;
    }
    return 65536 - high;
}
} // namespace openosc::plot
