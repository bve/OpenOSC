#include "openosc/ui/scope/meter_scale.hpp"
#include <cmath>

namespace openosc::plot {
double MeterScale::perDivision(int step) {
    static constexpr double mantissas[] = {1, 2, 5};
    const int decade = step >= 0 ? step / 3 : -((2 - step) / 3);
    return mantissas[step - decade * 3] * std::pow(10.0, decade);
}
bool MeterScale::step(unsigned index, double value, int8_t &output) {
    if (index >= Quantities || !std::isfinite(value) || value <= 0) return false;
    for (int step = Spans[index].lowest; step <= Spans[index].highest; ++step)
        if (std::fabs(perDivision(step) - value) <= value * 1e-6) {
            output = int8_t(step);
            return true;
        }
    return false;
}
bool MeterScale::fit(unsigned index, double minimum, double maximum, double zero, int8_t &output) {
    if (index >= Quantities) return false;
    output = Spans[index].highest;
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum > maximum) return false;
    for (int step = Spans[index].lowest; step <= Spans[index].highest; ++step) {
        const double divisions = perDivision(step);
        if (maximum / divisions + zero <= 4 && minimum / divisions + zero >= -4) {
            output = int8_t(step);
            return true;
        }
    }
    return false;
}
} // namespace openosc::plot
