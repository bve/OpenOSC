#pragma once
#include "openosc/data/units.hpp"
#include <cstdint>

namespace openosc::plot {
// The DMM trace's units/div in 1-2-5 steps of the reading's SI unit: step 0 is one unit a division,
// steps 1, 2, 3 are 2, 5, 10, step -1 is 0.5. Each quantity has a span of its own that covers the
// readings of its functions on both boards.
class MeterScale {
public:
    // The quantities with a scale: Voltage .. Temperature (Quantity 1-6), in that order.
    static constexpr unsigned Quantities = 6;
    static constexpr bool scaled(Quantity quantity) { return quantity > Quantity::Unknown && quantity <= Quantity::Temperature; }
    static constexpr unsigned index(Quantity quantity) { return unsigned(quantity) - 1; } // Of a scaled() quantity.
    struct Span {
        int8_t lowest, highest, initial; // Steps.
        const char *unit, *key;          // The SI unit's symbol (temperature in degrees Celsius); the JSON key.
    };
    static constexpr Span Spans[Quantities] = {
        {-9, 8, 0, "V", "voltage"},       // 1 mV .. 500 V, 1 V
        {-18, 2, -3, "A", "current"},     // 1 uA .. 5 A, 100 mA
        {0, 23, 9, "Ω", "resistance"},    // 1 Ohm .. 50 MOhm, 1 kOhm
        {-33, -4, -18, "F", "capacitance"}, // 10 pF .. 50 mF, 1 uF
        {0, 23, 9, "Hz", "frequency"},    // 1 Hz .. 50 MHz, 1 kHz
        {0, 8, 3, "°C", "temperature"}    // 1 .. 500 degC, 10 degC
    };
    static bool valid(unsigned index, int step) { return step >= Spans[index].lowest && step <= Spans[index].highest; }
    static double perDivision(int step);
    // The step of a units/div value within a quantity's span (a hand-edited file); false for none.
    static bool step(unsigned index, double perDivision, int8_t &output);
};
} // namespace openosc::plot
