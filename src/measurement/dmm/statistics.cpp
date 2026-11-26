#include "openosc/measurement/dmm/session.hpp"
#include "openosc/measurement/dmm/charge.hpp"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/data/decimal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace openosc::dmm {
bool Statistics::push(const NumericValue &value, int8_t exponent) {
    if (!value.valid() || !std::isfinite(value.si))
        return false;
    if (quantity_ != value.quantity)
        reset();
    if (count_ == std::numeric_limits<uint64_t>::max())
        return false;
    const double adjusted = value.si - correction_, next = sum_ + adjusted;
    if (!std::isfinite(adjusted) || !std::isfinite(next))
        return false;
    quantity_ = value.quantity;
    if (!count_) {
        min_ = max_ = value.si;
        minimumExponent_ = maximumExponent_ = averageExponent_ = exponent;
    } else if (averageExponent_ != DisplayPrefix::Unknown)
        averageExponent_ = exponent == DisplayPrefix::Unknown ? exponent : std::max(averageExponent_, exponent);
    if (value.si < min_) {
        min_ = value.si;
        minimumExponent_ = exponent;
    }
    if (value.si > max_) {
        max_ = value.si;
        maximumExponent_ = exponent;
    }
    correction_ = (next - sum_) - adjusted;
    sum_ = next;
    ++count_;
    return true;
}
StatisticsView StatisticsView::of(const Statistics &s, Quantity shown, double offset) {
    StatisticsView view;
    if (!s.count() || s.quantity() != shown) return view;
    view.valid = true; view.count = s.count(); view.quantity = s.quantity();
    view.minimum = s.minimum() - offset; view.maximum = s.maximum() - offset; view.average = s.average() - offset;
    view.minimumExponent = s.minimumExponent(); view.maximumExponent = s.maximumExponent(); view.averageExponent = s.averageExponent();
    return view;
}
bool ChargeCounter::counts(Function function) { return function == Function::CurrentDc || function == Function::MilliampsDc; }
void ChargeCounter::reset() {
    coulombs_ = amps_ = 0; atUs_ = 0;
    function_ = ~0U; current_ = linked_ = false;
}
void ChargeCounter::observe(const Result &r, uint64_t timestampUs, bool confirmed) {
    if (!confirmed) { linked_ = false; return; } // A selection in transition: no reset, no interval across it.
    // The function is followed on every confirmed packet, held or not: another one starts a new charge.
    const auto function = Functions::identify(r);
    if (function != Function::Unknown) {
        if (function_ != ~0U && (unsigned(function) != function_ || r.currentInput != current_)) reset();
        function_ = unsigned(function); current_ = r.currentInput;
    }
    const bool live = counts(function) && r.kind == Kind::Dc && r.primary.quantity == Quantity::Current &&
                      r.primary.valid() && !r.packetHold && !r.peerRelative;
    if (!live) { linked_ = false; return; } // Nothing joins across it.
    if (linked_ && timestampUs >= atUs_ && timestampUs - atUs_ <= MaximumGapUs)
        coulombs_ += (amps_ + r.primary.si) / 2 * double(timestampUs - atUs_) / 1e6;
    amps_ = r.primary.si; atUs_ = timestampUs; linked_ = true;
}
void ChargeCounter::format(double ampereHours, char *output, size_t capacity) {
    if (!output || !capacity) return;
    // Decimals by the rounded magnitude: 3 below 9.9995, 2 below 99.995, 1 below 999.95, then 0.
    const auto decimals = [](double value) {
        const double size = std::fabs(value);
        return size < 9.9995 ? 3U : size < 99.995 ? 2U : size < 999.95 ? 1U : 0U;
    };
    const bool milli = std::fabs(ampereHours * 1000) < 999.95;
    const double value = milli ? ampereHours * 1000 : ampereHours;
    char number[24];
    const int n = Decimal::fixed(value, decimals(value), number, sizeof number)
                      ? std::snprintf(output, capacity, "%s %s", number, milli ? "mAh" : "Ah") : -1;
    if (n < 0 || size_t(n) >= capacity) std::snprintf(output, capacity, "--");
}
} // namespace openosc::dmm
