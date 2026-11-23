#include "openosc/ui/scope/model.hpp"
#include <cmath>

namespace openosc::plot {
CursorReadout Model::cursors() const {
    CursorReadout out;
    if (!valid_ || options_.mode == Mode::XY) // X-Y has no time axis, and its two axes are two channels.
        return out;
    const auto &cursor = options_.cursors;
    if (options_.mode==Mode::Spectrum) {
        if (cursor.time && spectrum().valid) {
            out.deltaFrequencyHz=spectrumFrequency(cursor.xB)-spectrumFrequency(cursor.xA);
            out.frequencyValid=true;
        }
        if (cursor.voltage && spectrum().valid) {
            const double delta=(int(cursor.yA)-cursor.yB)/1000.0;
            if (options_.spectrum.scale==scope::SpectrumScale::Dbfs) {
                out.deltaDecibels=80*delta; out.decibelsValid=true;
            } else {
                out.deltaCodes=spectrum().maximum/.85*delta; out.codesValid=true;
                const auto &source=metadata_.channels[options_.spectrum.channel];
                if (source.calibrated) {
                    out.deltaVolts=out.deltaCodes*source.voltsPerCode*source.probeNumerator/source.probeDenominator;
                    out.voltsValid=true;
                }
            }
        }
        return out;
    }
    if (cursor.time && (options_.mode == Mode::Time || options_.mode == Mode::SpectrumOverlay)) {
        out.deltaSeconds = double(viewport_.spanQ16) / 65536 * (int(cursor.xB) - cursor.xA) / 1000 /
                           metadata_.samplesPerSecond.value();
        out.timeValid = std::isfinite(out.deltaSeconds);
        if (out.timeValid && out.deltaSeconds != 0) {
            out.reciprocalHz = 1 / std::abs(out.deltaSeconds);
            out.reciprocalValid = std::isfinite(out.reciprocalHz);
        }
    }
    if (cursor.channel == CursorOptions::Meter) return out; // The DMM trace's levels: Painter::cursors.
    const auto &channel = metadata_.channels[cursor.channel];
    if (cursor.voltage && channel.enabled && options_.channels[cursor.channel].visible &&
        axes_[cursor.channel].factor) {
        const auto &axis = axes_[cursor.channel];
        out.deltaCodes = axis.code(cursorPixel(cursor.yB, viewport_.height - 1)) -
                         axis.code(cursorPixel(cursor.yA, viewport_.height - 1));
        out.codesValid = std::isfinite(out.deltaCodes);
        if (channel.calibrated) {
            out.deltaVolts =
                out.deltaCodes * channel.voltsPerCode * channel.probeNumerator / channel.probeDenominator;
            out.voltsValid = std::isfinite(out.deltaVolts);
        }
    }
    return out;
}
} // namespace openosc::plot
