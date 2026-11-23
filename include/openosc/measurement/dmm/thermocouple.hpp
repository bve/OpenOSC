/*************************************************************************
Title:    Thermocouple Direct Measurement Library
Authors:  Nathan D. Holmes <maverick@drgw.net>
Authors:  Michael D. Petersen <railfan@drgw.net>
File:     Thermocouple.h
License:  GNU General Public License v3

CREDITS:
    This library was hugely based on the work of Mosiac Industries and their
    development of improved rational polynomial functions and coefficients
    to model thermocouple systems.  Their work provides significantly lower
    computational costs and significantly lower error as compared to the 
    NIST ITS-90 polynomials and coefficients.  See their work here:
    http://www.mosaic-industries.com/embedded-systems/microcontroller-projects/temperature-measurement/thermocouple/calibration-table

LICENSE:
    Copyright (C) 2021 Nathan D. Holmes & Michael D. Petersen

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    Ported from the original library into OpenOSC, with these changes:
    - corrected to match Mosaic's tables: the type B 250-700 degC segment's p3 sign,
      the type T 200-400 degC segment's q3, and the type K upper limit, 54.886 mV
      (the end of the NIST table);
    - the cold junction EMF is computed from degree-6 least-squares fits of the NIST ITS-90
      reference functions over -40..150 degC (type B from 0 degC) instead of Mosaic's rational
      fits, which have poles inside the junction range; the same coefficients as in the kiln
      controller firmware;
    - a cold junction temperature window per type (-40..100 degC, type B 0..100 degC), outside
      which a conversion is refused.

*************************************************************************/
#pragma once
#include <cstdint>

namespace openosc::dmm {
enum class ThermocoupleType : uint8_t { B, E, J, K, N, R, S, T, Count };

// The cold junction temperatures (degC, both ends inclusive) a type's conversion accepts, within the range its fit covers.
struct JunctionWindow { int from, to; };

class Thermocouple {
public:
    static JunctionWindow junctionWindow(ThermocoupleType type);
    // Whether the cold junction temperature is within the type's window (junctionWindow).
    static bool junctionInRange(ThermocoupleType type, double junctionCelsius);
    // Temperature of a thermocouple of the given type whose terminals read `volts`, with the cold
    // junction at `junctionCelsius`. False outside the voltage range the type's polynomials are fitted to,
    // and outside the cold junction window of the type (junctionInRange).
    static bool convert(double volts, double junctionCelsius, ThermocoupleType type, double &celsius);
};
} // namespace openosc::dmm
