#include "openosc/ui/scope/viewport.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
bool Viewport::calculate(const scope::FrameMetadata &m, const Options &o, unsigned w, unsigned h, uint32_t phase) {
    if (!m.valid() || !o.valid() || w < 2 || h < 2 || w > MaximumWidth || h > MaximumHeight)
        return false;
    width = w;
    height = h;
    // A held record may be shown at the next capture's time/div (Options::heldTimebaseNs); the
    // record's own timebase and sample rate stay what they are.
    const uint64_t timebase = m.timebaseNs && o.heldTimebaseNs ? o.heldTimebaseNs : m.timebaseNs;
    const uint64_t fullSpan = timebase
        ? uint64_t(std::llround(timebase/100000000.0*m.samplesPerSecond.value()*65536))
        : uint64_t(m.samples-1)<<16;
    const uint64_t record = uint64_t(m.samples - 1) << 16;
    spanQ16 = std::max<uint64_t>(65536, fullSpan * o.zoomOut / o.zoom);
    if (o.zoomOut > 1) spanQ16 = std::min(spanQ16, record);
    if (timebase != m.timebaseNs) spanQ16 = std::min(spanQ16, MaximumHeldSpanQ16);
    // A trigger-locked time view of a shifted trigger (FrameMetadata::triggerShift) counts positions
    // from the record as it would be cut at its pretrigger point: pan, the lock and the zoom-out
    // bounds place it as an unshifted record, its missing part outside the record (RecordGaps). Any
    // other view (unlocked, X-Y, FFT) keeps the real record's origin and the trigger's own fraction.
    const bool anchored = o.lockTrigger && m.triggered && (o.mode == Mode::Time || o.mode == Mode::SpectrumOverlay);
    // Center trigger on a held record: its trigger, or an untriggered one's pretrigger point (#65).
    const bool centered = o.centerHeldTrigger && o.lockTrigger && (o.mode == Mode::Time || o.mode == Mode::SpectrumOverlay) &&
                          !m.rolling && (m.triggered || m.pretriggerKnown);
    const int64_t origin = anchored ? int64_t(m.triggerShift) * 65536 : 0;
    const int64_t full = int64_t(record), pan = o.panPpb; // record < 2^37: both products fit int64.
    startQ16 = origin + full / Options::PanFull * pan + full % Options::PanFull * pan / Options::PanFull;
    if (m.rolling) startQ16+=int64_t(m.samples-1)*65536-int64_t(spanQ16);
    phaseQ16 = 0;
    if (o.lockTrigger && m.triggered) {
        const uint64_t triggerPosition = centered ? spanQ16 / 2 : pretrigger(m, anchored);
        startQ16 += int64_t(m.triggerIndex) * 65536 - origin - int64_t(triggerPosition);
        // The crossing, not the first sample past it, at the trigger's place (GitHub #34).
        if (phase < 65536 && phased(m, o)) { phaseQ16 = phase; startQ16 -= phase; }
    }
    // Without a trigger, Auto still needs captured history on both sides of the initial window: its
    // pretrigger point (#65) is placed as a trigger there would be, so the marker keeps its place at
    // any span (issue #89: a centred window put it up to half a point left). A record without one is
    // centred. X-Y and FFT retain their own origin; explicit free positioning is unchanged.
    else if (!m.rolling && !m.triggered && o.lockTrigger &&
             (o.mode == Mode::Time || o.mode == Mode::SpectrumOverlay))
        startQ16 += centered ? int64_t(m.pretrigger) * 65536 - int64_t(spanQ16 / 2)
                  : m.pretriggerKnown ? int64_t(m.pretrigger) * 65536 - int64_t(place(m, m.pretrigger))
                  : (int64_t(record) - int64_t(spanQ16)) / 2;
    // An explicitly centered held trigger can leave part of the viewport outside the record.
    if (o.zoomOut > 1 && !centered) startQ16 = std::clamp<int64_t>(startQ16, origin, origin + int64_t(record - spanQ16));
    return true;
}
bool Viewport::phased(const scope::FrameMetadata &m, const Options &o) const {
    return o.lockTrigger && m.triggered && (o.mode == Mode::Time || o.mode == Mode::SpectrumOverlay) && !aggregates();
}
uint64_t Viewport::pretrigger(const scope::FrameMetadata &m, bool anchored) const {
    if (m.samples < 2) return 0;
    // The record's pretrigger point (anchored: where a shifted trigger should have been), else its trigger.
    const int64_t anchor = int64_t(m.triggerIndex) - (anchored ? m.triggerShift : 0);
    return place(m, uint32_t(std::clamp<int64_t>(anchor, 0, int64_t(m.samples - 1))));
}
uint64_t Viewport::place(const scope::FrameMetadata &m, uint32_t point) const {
    if (m.samples < 2) return 0;
    const uint64_t last = m.samples - 1, index = std::min<uint64_t>(point, last);
    return spanQ16 / last * index + spanQ16 % last * index / last;
}
void Viewport::column(unsigned x, uint32_t samples, uint32_t &first, uint32_t &last) const {
    const uint64_t length = spanQ16 + 65536;
    const int64_t start = floorSample(startQ16 + int64_t(length * x / width));
    const int64_t end = floorSample(startQ16 + int64_t(length * (x + 1) / width));
    first = uint32_t(std::clamp<int64_t>(start, 0, samples));
    last = uint32_t(std::clamp<int64_t>(end, 0, samples));
}
int64_t Viewport::position(unsigned x) const { return startQ16 + int64_t(spanQ16 * x / (width - 1)); }
int Viewport::positionX(int64_t positionQ16) const {
    if (!spanQ16 || width < 2)
        return 0;
    const int64_t value = (positionQ16 - startQ16) * (width - 1) / int64_t(spanQ16);
    return int(std::clamp<int64_t>(value, -4096, 4096));
}
namespace {
double heldMillivolts(const scope::ChannelMetadata &m, const ChannelOptions &o) {
    return double(o.heldMillivoltsAtTip) * m.probeDenominator / m.probeNumerator;
}
bool drawable(double scale) { return std::isfinite(scale) && scale >= 1 && scale <= 16777216; }
} // namespace
bool Axis::held(const scope::ChannelMetadata &m, const ChannelOptions &o) {
    return o.heldMillivoltsAtTip > 0 && m.calibrated && m.probeNumerator && m.probeDenominator &&
           drawable(heldMillivolts(m, o) * .256 / m.voltsPerCode);
}
double Axis::millivolts(const scope::ChannelMetadata &m, const ChannelOptions &o) {
    return held(m, o) ? heldMillivolts(m, o) : m.millivoltsPerDivision;
}
bool Axis::configure(const scope::ChannelMetadata &m, const ChannelOptions &o, unsigned height) {
    zeroQ8 = 0;
    codesPerDivisionQ8 = 8192;
    double scale = codesPerDivisionQ8;
    if (m.calibrated) {
        scale = millivolts(m, o) * .256 / m.voltsPerCode;
        if (!drawable(scale) || std::abs(m.zeroCode) > 32768)
            return false;
        codesPerDivisionQ8 = int32_t(std::lround(scale));
        zeroQ8 = int32_t(std::lround(m.zeroCode * 256));
    }
    basePermille = base(m);
    origin = int16_t(int32_t(height - 1) * (basePermille + o.positionPermille - 500) / 1000);
    // The pixel transform is evaluated on projected points and extrema. From the unrounded codes per
    // division: a held V/div far finer than the record's own (a few Q8 codes a division) keeps its scale.
    factor = std::llround(double(height - 1) * 4294967296.0 * o.gainPercent / (800 * scale));
    if (o.invert)
        factor = -factor;
    return factor != 0;
}
int16_t Axis::base(const scope::ChannelMetadata &m) {
    // A calibrated record compensates its codes for the analog zero, so the zero's line has to be
    // added here. Raw codes already carry it: adding it again would move the trace twice.
    return m.calibrated ? int16_t(std::clamp<int>(m.analogPosition, 0, 160) * 1000 / 160) : 500;
}
int16_t Axis::pixel(int32_t raw) const {
    int64_t offset;
    if (__builtin_mul_overflow(int64_t(raw) - zeroQ8, factor, &offset)) // Far off the plot either way.
        return ((int64_t(raw) - zeroQ8 > 0) == (factor > 0)) ? -4096 : 4096;
    const int64_t value = int64_t(origin) - offset / (int64_t(1) << 32);
    return int16_t(std::clamp<int64_t>(value, -4096, 4096));
}
double Axis::code(int y) const { return factor ? zeroQ8 / 256.0 + (origin - y) * 16777216.0 / double(factor) : 0; }
} // namespace openosc::plot
