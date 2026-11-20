#pragma once
#include "openosc/measurement/dmm/scale.hpp"
#include <cmath>

namespace openosc::dmm {
// Temperature uses signed, labelled bands in the displayed unit. Counts are tenths of a degree.
class TemperatureAxis {
public:
    static constexpr int CountsPerDegree = 10;
    static constexpr unsigned Decimals = 1;

    CountScale select(double degrees, bool fahrenheit, bool relative) {
        if (fahrenheit != fahrenheit_ || relative != relative_) {
            reset();
            fahrenheit_ = fahrenheit;
            relative_ = relative;
        }
        if (std::isfinite(degrees)) {
            const double counts = degrees * CountsPerDegree;
            const int margin = (fahrenheit ? 2 : 1) * CountsPerDegree;
            if (!known_ || counts < scale_.minimum - margin || counts > scale_.minimum + scale_.fullScale + margin)
                scale_ = band(degrees);
            known_ = true;
        }
        return known_ ? scale_ : makeScale(0, 100, 20);
    }
    void reset() { known_ = false; }

private:
    static constexpr int MinorTicksPerLabel = 5;
    static constexpr CountScale makeScale(int low, int high, int labelStep) {
        return {(high - low) * CountsPerDegree, MinorTicksPerLabel,
                low * CountsPerDegree, labelStep * CountsPerDegree / MinorTicksPerLabel};
    }
    static CountScale band(double degrees) {
        static constexpr int endpoints[] = {0, 100, 300, 600, 1000, 1500, 2000, 2500, 3000};
        const double magnitude = std::fabs(degrees);
        unsigned upper = 1;
        while (upper + 1 < sizeof endpoints / sizeof *endpoints && magnitude > endpoints[upper]) ++upper;
        const int low = endpoints[upper - 1], high = endpoints[upper];
        const int step = high <= 100 ? 20 : high <= 300 ? 50 : 100;
        return degrees < 0 ? makeScale(-high, -low, step) : makeScale(low, high, step);
    }

    CountScale scale_ = BoardScale;
    bool known_ = false, fahrenheit_ = false, relative_ = false;
};
} // namespace openosc::dmm
