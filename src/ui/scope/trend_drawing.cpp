#include "openosc/ui/scope/painter.hpp"
#include "openosc/acquisition/trend.hpp"
#include "openosc/ui/scope/meter_readouts.hpp"
#include "openosc/ui/scope/view_zoom.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::plot {
// A trend (TrendBuffer) is drawn from the points it keeps, which the frame's means cannot show: each
// column is the min and max of its points as a band and their mean as a line. The copy places its own
// points over the whole plot, so it is drawn whenever it is valid, whichever frame the model holds
// meanwhile, while the view is the record's own: zoomed, panned or held at the next capture's time/div
// (issue #7) the frame's means are drawn instead, as the time/div label and the cursors follow that view.
bool Painter::trendShown() const {
    const auto &o = model_.options();
    return trend_ && trend_->points && trend_->sequence && model_.metadata().rolling && model_.viewport().width >= 2 &&
           !o.heldTimebaseNs && ViewZoom::home(o);
}
RecordGaps Painter::trendGaps() const {
    RecordGaps gaps;
    gaps.width = model_.viewport().width;
    gaps.before = trend_->before(gaps.width);
    return gaps;
}
// A column with no point (before the record's start) breaks the line.
void Painter::trendTrace(unsigned channel, uint32_t color, uint8_t opacity, unsigned width) {
    if (!model_.storage().traces[channel].enabled || !opacity) return;
    const auto &view = model_.viewport();
    const auto &axis = model_.axis(channel);
    Path band(layer_, area_, color, uint8_t(65U * opacity / 255), 1, model_.statistics());
    Path line(layer_, area_, color, opacity, width, model_.statistics());
    for (unsigned x = 0; x < view.width; ++x) {
        uint32_t first, last;
        trend_->column(x, view.width, first, last);
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
        const double mean = double(sum) / double(trend_->adcPoints(first, last)); // A last point STOP cut short holds fewer.
        // A higher code is a smaller y: the band runs from the pixel of the high to that of the low.
        band.vertex(x, axis.pixel(high * 256));
        band.vertex(x, axis.pixel(low * 256) + 1);
        band.gap();
        line.vertex(x, axis.pixel(int32_t(std::lround(mean * 256))));
    }
}
// The DMM on its own scale and zero, as meterTrace's line. A column holds the readings of its points: the
// count-weighted mean of their means between the lowest and highest. One with no reading is skipped
// (the line joins over it: a point is much shorter than the chip's 320 ms early in a record); a NaN
// reading or a gap mark among its points (OL, a stop of the readings) breaks the line before it.
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
    Path band(layer_, area_, palette_.meter, 65, 1, model_.statistics());
    Path line(layer_, area_, palette_.meter, 255, 1, model_.statistics());
    for (unsigned x = 0; x < view.width; ++x) {
        uint32_t first, last;
        trend_->column(x, view.width, first, last);
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
        if (missing) line.gap();
        if (!count) continue;
        band.vertex(x, place(high));
        band.vertex(x, place(low) + 1);
        band.gap();
        line.vertex(x, place(weighted / count));
    }
}
} // namespace openosc::plot
