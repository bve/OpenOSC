#pragma once
#include "openosc/acquisition/measurements.hpp"
#include "openosc/ui/scope/model.hpp"

namespace openosc::dmm {
class ReadingTrace;
}
namespace openosc::plot {
// Where a reading of the DMM trace (dmm::ReadingTrace) lies on a record's time axis: in record points
// after its first, which was taken at FrameMetadata::timestampUs (the readings' clock), at its rate.
// last: the record's last point.
struct MeterAxis {
    explicit MeterAxis(const scope::FrameMetadata &m)
        : originMs(uint32_t(m.timestampUs / 1000)), originUs(double(m.timestampUs % 1000)), rate(m.samplesPerSecond.value()),
          last(m.samples > 1 ? double(m.samples - 1) : 0) {}
    bool valid() const { return rate > 0 && last > 0; }
    double point(uint32_t ms) const { return (double(int32_t(ms - originMs)) * 1000 - originUs) * rate / 1e6; }
    uint32_t originMs;
    double originUs, rate, last;
};
// The Scope's DMM trace for the footer (ScopeFooter::meter), each display pass: whether it is shown (a
// Y-T or Y-T+FFT view), its scale and its readouts (MeterReadoutSelection) over the visible window.
// They are of the readings its line draws there, up to the record's last point; a window that holds
// none (shorter than the readings' interval) has the one in force at its start, its level. RMS is
// offered unless the DMM function is an AC one, whose readings are RMS values already.
struct MeterReadouts {
    bool shown = false, valid = false, rmsOffered = false;
    double mean = 0, minimum = 0, maximum = 0, rms = 0;
    double perDivision = 0; // Its units/div (MeterScale); 0 until a reading of a scaled quantity.
    const char *unit = "", *function = ""; // MeterScale's SI unit; the DMM function's name.
    bool offers(scope::Measurement kind) const { return kind != scope::Measurement::Rms || rmsOffered; }
    bool value(scope::Measurement kind, double &output) const;
    // The scale and readouts of `readings` over the window `model` shows (shown, rmsOffered and
    // function are the caller's).
    void calculate(const dmm::ReadingTrace &readings, const Model &model);
    bool operator==(const MeterReadouts &o) const;
};
} // namespace openosc::plot
