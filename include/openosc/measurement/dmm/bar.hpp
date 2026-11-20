#pragma once
#include "openosc/measurement/dmm/relative.hpp"
#include "openosc/measurement/dmm/resolution.hpp"
#include "openosc/measurement/dmm/temperature_axis.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace openosc::dmm {
// The magnitude the bar and the dial show, in counts of the reading's own scale (scaleFor: the
// board's, capacitance its 9999-count one), clamped to its full scale; overload pegs it with its sign.
struct BarReading {
    CountScale scale = BoardScale;
    int counts = 0;
    DataStatus status = DataStatus::NoData;
    bool temperature = false;
    // Position within the scale. Temperature counts are measured from its signed lower bound.
    float fraction() const { return float(std::abs(counts)) / float(scale.fullScale); }
    static BarReading from(const Result &result, DataStatus health,
                           const RelativeReading *relative = nullptr, TemperatureAxis *axis = nullptr) {
        BarReading bar;
        bar.scale = scaleFor(result.primary.quantity);
        bar.temperature = result.primary.quantity == Quantity::Temperature;
        bar.status = health == DataStatus::Ok ? result.primary.status : health;
        const bool delta = relative && relative->matches(result);
        if (bar.temperature) {
            const bool live = bar.status == DataStatus::Ok && result.primary.valid();
            const double degrees = live ? TypeK::display(delta ? relative->apply(result).si : result.primary.si,
                                                         result.unit, delta || result.peerRelative)
                                        : std::numeric_limits<double>::quiet_NaN();
            TemperatureAxis singleReading;
            bar.scale = (axis ? *axis : singleReading).select(degrees, TypeK::fahrenheit(result.unit), delta || result.peerRelative);
            if (std::isfinite(degrees))
                bar.counts = int(std::lround(std::clamp(degrees * TemperatureAxis::CountsPerDegree - bar.scale.minimum,
                                                        0., double(bar.scale.fullScale))));
            else if (bar.status == DataStatus::Overload) bar.counts = bar.scale.fullScale;
            else if (bar.status == DataStatus::Ok) bar.status = DataStatus::Unsupported;
            return bar;
        }
        if (axis && result.primary.quantity != Quantity::Unknown) axis->reset();
        const int full = bar.scale.fullScale;
        if (bar.status == DataStatus::Overload) {
            bar.counts = result.mainText[0] == '-' ? -full : full;
            return bar;
        }
        if (bar.status != DataStatus::Ok) return bar;
        if (!result.primary.valid()) { bar.status = DataStatus::Unsupported; return bar; }
        double magnitude = result.rawMagnitude;
        bool negative = result.primary.si < 0;
        if (delta) {
            NumericValue unit;
            const unsigned decimals = Resolution::decimals(result);
            if (decimals > 4 || !UnitParser::parse("1", result.unit, unit) || unit.si <= 0) {
                bar.status = DataStatus::Unsupported; return bar;
            }
            double value = relative->apply(result).si / unit.si;
            negative = value < 0;
            for (unsigned i = 0; i < decimals; ++i) value *= 10;
            magnitude = std::abs(value);
        }
        if (!std::isfinite(magnitude)) { bar.status = DataStatus::Unsupported; return bar; }
        bar.counts = int(std::lround(std::clamp(magnitude, 0., double(full))));
        if (negative) bar.counts = -bar.counts;
        return bar;
    }
};
} // namespace openosc::dmm
