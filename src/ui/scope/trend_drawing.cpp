#include "openosc/ui/scope/painter.hpp"
#include "openosc/acquisition/trend.hpp"
#include "openosc/ui/scope/meter_readouts.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
// A trend (TrendBuffer) is drawn from the points it keeps, which the frame's means cannot show: each
// column is the min and max of its points as a band and their mean as a line. The copy was published with
// the frame, so it is drawn only while their sequences match.
bool Painter::trendShown() const {
    const auto &metadata = model_.metadata();
    return trend_ && trend_->points && trend_->sequence && trend_->sequence == metadata.sequence && trend_->size == metadata.samples &&
           metadata.rolling && model_.viewport().width >= 2;
}
// A column with no point (before the record's start) breaks the line.
void Painter::trendTrace(unsigned channel, uint32_t color, uint8_t opacity, unsigned width) {
    if (!model_.storage().traces[channel].enabled || !opacity) return;
    const auto &view = model_.viewport();
    const auto &axis = model_.axis(channel);
    const uint32_t samples = model_.metadata().samples;
    Path band(layer_, area_, color, uint8_t(65U * opacity / 255), 1, model_.statistics());
    Path line(layer_, area_, color, opacity, width, model_.statistics());
    for (unsigned x = 0; x < view.width; ++x) {
        uint32_t first, last;
        view.column(x, samples, first, last);
        if (first >= last) {
            line.gap();
            continue;
        }
        int low = 127, high = -128;
        int64_t sum = 0;
        for (uint32_t i = first; i < last; ++i) {
            low = std::min<int>(low, trend_->points->low[channel][i]);
            high = std::max<int>(high, trend_->points->high[channel][i]);
            sum += trend_->points->sum[channel][i];
        }
        const double mean = double(sum) / (double(trend_->perPoint) * (last - first));
        // A higher code is a smaller y: the band runs from the pixel of the high to that of the low.
        band.vertex(x, axis.pixel(high * 256));
        band.vertex(x, axis.pixel(low * 256) + 1);
        band.gap();
        line.vertex(x, axis.pixel(int32_t(std::lround(mean * 256))));
    }
}
// The DMM on its own scale and zero, as meterTrace's line. A column holds the readings of its points: the
// count-weighted mean of their means between the lowest and highest. One with no reading is skipped
// (the line joins over it: a point is much shorter than the chip's 320 ms early in a record); one with
// only NaN readings or gap marks breaks it.
void Painter::trendMeter() {
    const auto &o = model_.options();
    const auto &view = model_.viewport();
    if (!o.meter.shown || !MeterScale::scaled(trend_->quantity) || view.height < 2) return;
    const double perDivision = MeterScale::perDivision(o.meter.scales[MeterScale::index(trend_->quantity)]);
    const double division = double(view.height - 1) / 8, zero = meterZero(), bottom = double(view.height - 1);
    const auto pixel = [](double v) { return int(std::lround(std::clamp(v, -4096.0, 4096.0))); };
    const auto place = [&](double value) {
        const double y = zero - value / perDivision * division;
        meterAbove_ = meterAbove_ || y < 0;
        meterBelow_ = meterBelow_ || y > bottom;
        return pixel(y);
    };
    const uint32_t samples = model_.metadata().samples;
    Path band(layer_, area_, palette_.meter, 65, 1, model_.statistics());
    Path line(layer_, area_, palette_.meter, 255, 1, model_.statistics());
    for (unsigned x = 0; x < view.width; ++x) {
        uint32_t first, last;
        view.column(x, samples, first, last);
        double weighted = 0, low = 0, high = 0;
        uint32_t count = 0, missing = 0;
        for (uint32_t i = first; i < last; ++i) {
            missing += trend_->points->dmmMissing[i];
            const uint32_t n = trend_->points->dmmCount[i];
            if (!n) continue;
            weighted += double(trend_->points->dmmMean[i]) * n;
            low = count ? std::min<double>(low, trend_->points->dmmLow[i]) : trend_->points->dmmLow[i];
            high = count ? std::max<double>(high, trend_->points->dmmHigh[i]) : trend_->points->dmmHigh[i];
            count += n;
        }
        if (!count) {
            if (missing) line.gap();
            continue;
        }
        band.vertex(x, place(high));
        band.vertex(x, place(low) + 1);
        band.gap();
        line.vertex(x, place(weighted / count));
    }
}
} // namespace openosc::plot
