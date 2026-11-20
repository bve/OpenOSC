#include "openosc/acquisition/ranges.hpp"
#include "openosc/calibration/data.hpp"

namespace openosc {
bool CalibrationData::scopeScale(unsigned channel, uint32_t millivolts, int position, double &volts,
                                 double &zero, double displayGain) const {
    // The word / 100 in double; an unwritten word: 1.20.
    return factoryScale(channel) && transfer(this->displayGain(channel), millivolts, position, displayGain, volts, zero);
}
bool CalibrationData::transfer(double gain, uint32_t millivolts, int position, double displayGain, double &volts, double &zero) {
    if (!(gain >= 1.0 && gain <= 1.99) || position < 0 || position > 160 || !scope::VoltageRanges::contains(millivolts) ||
        !(displayGain == 0 || (displayGain >= 1.0 && displayGain <= 1.99)))
        return false;
    // Factory transfer: 20 pixels/div and Y = 80 - .625 * gain * signed_code.
    // The offset table calibrates the zero-input code to the requested Y position.
    const double pixelsPerCode = .625 * gain;
    volts = millivolts / 1000.0 * .625 * (displayGain ? displayGain : gain) / 20;
    zero = (80 - position) / pixelsPerCode;
    return true;
}
} // namespace openosc
