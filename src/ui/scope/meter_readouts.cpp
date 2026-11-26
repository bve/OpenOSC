#include "openosc/ui/scope/meter_readouts.hpp"
#include "openosc/measurement/dmm/reading_trace.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace openosc::plot {
bool MeterReadouts::value(scope::Measurement kind, double &output) const {
    using M = scope::Measurement;
    if (!valid || !offers(kind)) return false;
    switch (kind) {
    case M::Mean: output = mean; return true;
    case M::Minimum: output = minimum; return true;
    case M::Maximum: output = maximum; return true;
    case M::PeakToPeak: output = maximum - minimum; return true;
    case M::Rms: output = rms; return true;
    default: return false;
    }
}
void MeterReadouts::calculate(const dmm::ReadingTrace &readings, const Model &model) {
    valid = false;
    perDivision = 0;
    unit = "";
    const auto quantity = readings.quantity();
    if (!MeterScale::scaled(quantity)) return;
    const unsigned index = MeterScale::index(quantity);
    perDivision = MeterScale::perDivision(model.options().meter.scales[index]);
    unit = MeterScale::Spans[index].unit;
    if (!model.hasFrame()) return;
    const MeterAxis axis(model.metadata());
    const auto &view = model.viewport();
    if (!axis.valid() || !view.spanQ16) return;
    // The window in record points, cut at the record's first and last points (Painter::meterTrace's line
    // spans the record: a reading before it holds its level from the record's start).
    const double start = double(view.startQ16) / 65536;
    const double first = std::max(start, 0.0), last = std::min(start + double(view.spanQ16) / 65536, axis.last);
    if (last < first) return;
    double sum = 0, squares = 0;
    unsigned count = 0;
    float before = NAN; // The reading in force at the window's start.
    const auto take = [&](double value) {
        minimum = count ? std::min(minimum, value) : value;
        maximum = count ? std::max(maximum, value) : value;
        sum += value;
        squares += value * value;
        ++count;
    };
    for (size_t i = 0; i < readings.size(); ++i) {
        const auto reading = readings[i];
        const double point = axis.point(reading.ms);
        if (point > last) break;
        if (point < first) before = reading.value;
        else if (std::isfinite(reading.value)) take(reading.value);
        else before = NAN; // A gap in the window: no level is held across it.
    }
    if (!count && std::isfinite(before)) take(before);
    if (!count) return;
    valid = true;
    mean = sum / count;
    rms = std::sqrt(squares / count);
}
bool MeterReadouts::operator==(const MeterReadouts &o) const {
    return shown == o.shown && valid == o.valid && rmsOffered == o.rmsOffered && mean == o.mean && minimum == o.minimum &&
           maximum == o.maximum && rms == o.rms && perDivision == o.perDivision && !std::strcmp(unit, o.unit) &&
           !std::strcmp(function, o.function);
}
} // namespace openosc::plot
