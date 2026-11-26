#include "openosc/ui/editors/frequency_editor.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::ui {
void FrequencyEditor::begin(uint64_t millihertz, uint64_t maximum, uint64_t minimum, bool samples) {
    samples_ = samples;
    maximum_ = std::clamp<uint64_t>(maximum, 1, samples ? 200000000000ULL : 10000000000ULL);
    minimum_ = std::clamp<uint64_t>(minimum, 1, maximum_);
    value_ = std::clamp<uint64_t>(millihertz, minimum_, maximum_);
    power_ = 3;
    unit_ = value_ >= 1000000000 ? 2 : value_ >= 1000000 ? 1 : 0;
}
uint64_t FrequencyEditor::step() const {
    uint64_t result = 1;
    for (unsigned i = 0; i < power_; ++i) result *= 10;
    return result;
}
void FrequencyEditor::move(int direction) {
    if (!direction) return;
    power_ = unsigned(std::clamp<int>(int(power_) + (direction < 0 ? 1 : -1), samples_ ? 3 : 0, int(digits()) - 1));
}
void FrequencyEditor::adjust(int direction) {
    if (!direction) return;
    const int64_t next = int64_t(value_) + int64_t(step()) * (direction > 0 ? 1 : -1);
    value_ = uint64_t(std::clamp<int64_t>(next, int64_t(minimum_), int64_t(maximum_)));
}
// The firmware's printf (newlib-nano) has no 64-bit conversions ("%llu" printed "lu"), so both parts
// go as 32-bit unsigned long: value_ <= 2e11 (begin), so the whole part is at most 2e8 (S/s), 2e5 or
// 200 and the fraction below the scale, 1e9 at most: each below 2^32. A sample rate's thousandths are
// zero and not shown ("1024000" S/s, "1.024000" MS/s).
void FrequencyEditor::text(char *output, size_t capacity) const {
    uint64_t scale = 1000;
    for (unsigned i = 0; i < unit_; ++i) scale *= 1000;
    static_assert(200000000000ULL / 1000 < (1ULL << 32) && 1000000000ULL < (1ULL << 32), "Both parts fit 32 bits");
    const auto whole = static_cast<unsigned long>(value_ / scale);
    if (!fractionDigits()) { std::snprintf(output, capacity, "%0*lu", int(integerDigits()), whole); return; }
    std::snprintf(output, capacity, "%0*lu.%0*lu", int(integerDigits()), whole, int(fractionDigits()),
                  static_cast<unsigned long>(value_ % scale / (samples_ ? 1000 : 1)));
}
} // namespace openosc::ui
