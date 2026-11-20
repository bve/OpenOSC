#include "openosc/ui/scope/model.hpp"
#include "openosc/ui/scope/trigger_phase.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace openosc::plot {
void Model::reset() {
    valid_ = false;
    dirty_ = true;
    reference_ = false;
    for (auto &t : storage_.traces)
        t.enabled = false;
    clearHistory();
    xyCount_ = 0;
    spectrumDirty_ = true;
    statistics_ = {};
    spectrum_.result={};
    spectrum_.windowCount=0;
}
void Model::clearHistory() {
    storage_.persistence.clear();
    std::memset(storage_.averageValid, 0, sizeof storage_.averageValid);
    for (auto &trace : storage_.peaks)
        trace.enabled = false;
}
void Model::clear() {
    spectrum_.result={};
    valid_ = false;
    dirty_ = true;
    xyCount_ = 0;
    reference_ = false;
    clearHistory();
    for (auto &t : storage_.traces)
        t.enabled = false;
}
bool Model::configure(const Options &options) {
    if (!options.valid())
        return false;
    if (!options_.sameTransform(options)) {
        clearHistory();
        reference_ = false;
    }
    if (options_.peakHold != options.peakHold)
        for (auto &trace : storage_.peaks)
            trace.enabled = false;
    if (options_.persistenceMs != options.persistenceMs) clearHistory();
    spectrumDirty_ = spectrumDirty_ || options_.spectrum.channel != options.spectrum.channel ||
        options_.spectrum.window != options.spectrum.window || options_.spectrum.removeDc != options.spectrum.removeDc;
    options_ = options;
    dirty_ = true;
    return true;
}
bool Model::project(const scope::FrameView &frame, unsigned width, unsigned height, uint32_t now,
                    const ColumnCache *cache) {
    if (!frame.valid()) {
        clear();
        return false;
    }
    Viewport next;
    if (!next.calculate(frame.metadata(), options_, width, height)) {
        clear();
        return false;
    }
    const auto &m = frame.metadata();
    const bool fresh = !valid_ || m.sequence != metadata_.sequence || m.timestampUs != metadata_.timestampUs;
    bool reset = width != viewport_.width || height != viewport_.height || m.samples != metadata_.samples || m.ring != metadata_.ring ||
                 m.timebaseNs != metadata_.timebaseNs || m.rolling!=metadata_.rolling ||
                 m.interleaveOffset != metadata_.interleaveOffset || // The same codes read otherwise.
                 m.samplesPerSecond.numerator != metadata_.samplesPerSecond.numerator ||
                 m.samplesPerSecond.denominator != metadata_.samplesPerSecond.denominator;
    for (unsigned c = 0; c < 2; ++c) {
        const auto &a = m.channels[c];
        const auto &b = metadata_.channels[c];
        reset = reset || a.enabled != b.enabled || a.calibrated != b.calibrated ||
                a.millivoltsPerDivision != b.millivoltsPerDivision || a.probeNumerator != b.probeNumerator ||
                a.probeDenominator != b.probeDenominator || a.voltsPerCode != b.voltsPerCode ||
                a.zeroCode != b.zeroCode || a.analogPosition != b.analogPosition || a.coupling != b.coupling;
    }
    const auto &edge = m.triggerEdge, &before = metadata_.triggerEdge; // What the crossing is found for (#34).
    const bool alignment = m.origin != metadata_.origin || m.layout != metadata_.layout ||
                           m.triggered != metadata_.triggered || m.triggerIndex != metadata_.triggerIndex ||
                           edge.known != before.known || edge.level != before.level || edge.rising != before.rising ||
                           edge.channel != before.channel;
    if (!fresh && !dirty_ && !reset && !alignment)
        return true;
    if (!fresh && alignment)
        reset = true;
    if (reset) {
        clearHistory();
        reference_ = false;
    } else if (fresh && m.rolling) // Each Roll snapshot moves the samples under the columns: no frame average.
        std::memset(storage_.averageValid, 0, sizeof storage_.averageValid);
    // Only for a projection that runs (a new frame, a change): the drawn curve's crossing (GitHub #34).
    // A new frame's own crossing keeps its averages (they are aligned by it); the same frame with
    // another trigger tag was reset above as any re-annotated frame.
    if (next.phased(m, options_))
        if (const uint32_t phase = TriggerPhase::before(frame, options_)) next.calculate(m, options_, width, height, phase);
    viewport_ = next;
    metadata_ = m;
    statistics_.sourceReads = statistics_.columns = 0;
    for (unsigned c = 0; c < 2; ++c)
        if (!axes_[c].configure(m.channels[c], options_.channels[c], height)) {
            clear();
            return false;
        }
    spectrumDirty_ = spectrumDirty_ || fresh || reset || alignment;
    bool success = true;
    if (options_.mode == Mode::XY) success = projectXY(frame);
    else if (options_.mode != Mode::Spectrum) success = projectTime(frame,fresh,cache);
    if (options_.mode == Mode::Spectrum || options_.mode == Mode::SpectrumOverlay)
        success = success && projectSpectrum(frame,spectrumDirty_);
    storage_.persistence.resize(width,height);
    agePersistence(now);
    if (!success) {
        clear();
        return false;
    }
    if (options_.persistenceMs && fresh && options_.mode!=Mode::Spectrum) accumulate();
    if (fresh) {
        ++statistics_.frames;
        frameAt_ = now;
    }
    dirty_ = false;
    ++statistics_.projections;
    valid_ = true;
    return true;
}
void Model::trigger(scope::Channel channel, double codes) {
    if (unsigned(channel) < 2 && std::isfinite(codes) && std::abs(codes) < 32768) {
        triggerChannel_ = channel;
        triggerLevel_ = codes;
    }
}
bool Model::cursorLines(const CursorOptions &cursors) {
    auto candidate = options_;
    candidate.cursors = cursors;
    if (!candidate.valid()) return false;
    options_.cursors = cursors;
    return true;
}
bool Model::captureReference(unsigned channel) {
    if (!valid_ || (options_.mode != Mode::Time && options_.mode != Mode::SpectrumOverlay) || channel > 1 || !storage_.traces[channel].enabled)
        return false;
    storage_.reference = storage_.traces[channel];
    reference_ = true;
    return true;
}
} // namespace openosc::plot
