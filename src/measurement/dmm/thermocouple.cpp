/*************************************************************************
Title:    Thermocouple Direct Measurement Library
Authors:  Nathan D. Holmes <maverick@drgw.net>
Authors:  Michael D. Petersen <railfan@drgw.net>
File:     Thermocouple.cpp
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
    - corrected to match Mosaic's tables: the type B 250-700 degC segment's p3 sign and the type T
      200-400 degC segment's q3;
    - type K is not converted with Mosaic's segments: it keeps the NIST ITS-90 emf polynomial and
      its inversion (TypeK in temperature.hpp) the multimeter used before the other types were added,
      with the cold junction EMF from the same polynomial; its cold junction window is that
      polynomial's whole range, -270..1372 degC, so a manual junction is limited only by the
      setting's own -50..100 degC (TemperatureSettings), as before;
    - for the other types, the cold junction EMF is computed from degree-6 least-squares fits of the
      NIST ITS-90 reference functions over -40..150 degC (type B from 0 degC) instead of Mosaic's
      rational fits, which have poles inside the junction range; the same coefficients as in the
      kiln controller firmware;
    - a cold junction temperature window per type (-40..100 degC, type B 0..100 degC, type K as
      above), outside which a conversion is refused.

*************************************************************************/

#include "openosc/measurement/dmm/thermocouple.hpp"
#include "openosc/measurement/dmm/temperature.hpp"
#include <cstddef>

namespace openosc::dmm {
namespace {
// The cold junction EMF: least-squares fits of the NIST ITS-90 reference function over -40..150 degC
// (type B from 0 degC), as in the kiln controller. The temperature as a function of the voltage: Mosaic
// Industries rational polynomials, in segments of the voltage range (mV).
// Degree-6 polynomial of the cold junction EMF (mV), Horner order, in x = junction - offset.
struct ColdPiece { double offset; double c[7]; };
struct Segment { double from, to, T0, V0, p1, p2, p3, p4, q1, q2, q3; };
// The voltage range is [from, to) in every segment.
// E, N and T have a second piece for the junction below 0 degC; the others are one piece.
// Type K has no segments or cold junction piece: it is converted by TypeK (see Thermocouple::convert).
struct Curve { const ColdPiece *cold; const ColdPiece *belowZero; JunctionWindow window; const Segment *segments; size_t count; };

constexpr ColdPiece coldB = {25.0, {-2.4927981324480790e-03, 4.6302714467670097e-05, 5.8102221718641921e-06, -1.1795037305323430e-09, 1.3609279196808691e-12, -1.5999674036971072e-15, 6.2990347196738885e-19}};
constexpr ColdPiece coldEBelowZero = {0.0, {1.3687102362763309e-06, 5.8667483671778965e-02, 4.6106923793500032e-05, -6.7722601586658467e-07, -1.8060914246636854e-08, -2.7148020662375271e-10, -1.6575145515704903e-12}};
constexpr ColdPiece coldE = {25.0, {1.4951117128808018e+00, 6.0951918364234005e-02, 4.6061426919046771e-05, -1.5941897722416602e-10, -2.5234327116827404e-10, 6.2286571531946188e-13, -5.4693806383574467e-16}};
constexpr ColdPiece coldJ = {25.0, {1.2772884462944984e+00, 5.1752279895283415e-02, 2.4520136689640291e-05, -7.3459981779171614e-08, 1.1300997956737882e-10, -1.4025126366998953e-13, 1.6279545593162824e-16}};
constexpr ColdPiece coldNBelowZero = {0.0, {2.9826251964549816e-09, 2.6159110144921640e-02, 1.0958903113524906e-05, -9.3642486453409208e-08, -3.2589794499461300e-11, -2.1225663905360237e-12, -1.3115317655428424e-14}};
constexpr ColdPiece coldN = {25.0, {6.5864592737489569e-01, 2.6782495281549242e-02, 1.8143641962259768e-05, 2.2273735739506866e-08, -1.7967057100459376e-10, 4.7355375305730860e-13, -5.7635657558498462e-16}};
constexpr ColdPiece coldR = {25.0, {1.4057865117999935e-01, 5.9428095386722781e-03, 1.2252084245889623e-05, -2.0591510868792604e-08, 3.0401109689530508e-11, -3.9088645534085703e-14, 3.7252033030856605e-17}};
constexpr ColdPiece coldS = {25.0, {1.4259824110371527e-01, 5.9911648682080152e-03, 1.0965550642364931e-05, -2.0227331363778654e-08, 2.8306837697696038e-11, -2.9427391665452676e-14, 2.1034918855549535e-17}};
constexpr ColdPiece coldTBelowZero = {0.0, {-2.6334697126690494e-06, 3.8744214503407405e-02, 4.2779389575392228e-05, -9.9524952487023243e-08, 2.6059690660385163e-09, 1.0682566530248872e-10, 1.0332391615038068e-12}};
constexpr ColdPiece coldT = {25.0, {9.9198091305810987e-01, 4.0682721904193565e-02, 4.2056973267165989e-05, 4.6691675207842024e-08, -1.0153233904496349e-09, 5.5837202268041654e-12, -1.1561410304325088e-14}};
constexpr Segment segmentsB[] = {
    {0.291, 2.431, 5.0000000E+02, 1.2417900E+00, 1.9858097E+02, 2.4284248E+01, -9.7271640E+01, -1.5701178E+01, 3.1009445E-01, -5.0880251E-01, -1.6163342E-01},
    {2.431, 13.820, 1.2461474E+03, 7.2701221E+00, 9.4321033E+01, 7.3899296E+00, -1.5880987E-01, 1.2681877E-02, 1.0113834E-01, -1.6145962E-03, -4.1086314E-06},
};

constexpr Segment segmentsE[] = {
    {-9.835, -5.237, -1.1721668E+02, -5.9901698E+00, 2.3647275E+01, 1.2807377E+01, 2.0665069E+00, 8.6513472E-02, 5.8995860E-01, 1.0960713E-01, 6.1769588E-03},
    {-5.237, 0.591, -5.0000000E+01, -2.7871777E+00, 1.9022736E+01, -1.7042725E+00, -3.5195189E-01, 4.7766102E-03, -6.5379760E-02, -2.1732833E-02, 0.0},
    {0.591, 24.964, 2.5014600E+02, 1.7191713E+01, 1.3115522E+01, 1.1780364E+00, 3.6422433E-02, 3.9584261E-04, 9.3112756E-02, 2.9804232E-03, 3.3263032E-05},
    {24.964, 53.112, 6.0139890E+02, 4.5206167E+01, 1.2399357E+01, 4.3399963E-01, 9.1967085E-03, 1.6901585E-04, 3.4424680E-02, 6.9741215E-04, 1.2946992E-05},
    {53.112, 76.373, 8.0435911E+02, 6.1359178E+01, 1.2759508E+01, -1.1116072E+00, 3.5332536E-02, 3.3080380E-05, -8.8196889E-02, 2.8497415E-03, 0.0},
};

constexpr Segment segmentsJ[] = {
    {-8.095, 0, -6.4936529E+01, -3.1169773E+00, 2.2133797E+01, 2.0476437E+00, -4.6867532E-01, -3.6673992E-02, 1.1746348E-01, -2.0903413E-02, -2.1823704E-03},
    {0, 21.840, 2.5066947E+02, 1.3592329E+01, 1.8014787E+01, -6.5218881E-02, -1.2179108E-02, 2.0061707E-04, -3.9494552E-03, -7.3728206E-04, 1.6679731E-05},
    {21.840, 45.494, 6.4950262E+02, 3.6040848E+01, 1.6593395E+01, 7.3009590E-01, 2.4157343E-02, 1.2787077E-03, 4.9172861E-02, 1.6813810E-03, 7.6067922E-05},
    {45.494, 57.953, 9.2510550E+02, 5.3433832E+01, 1.6243326E+01, 9.2793267E-01, 6.4644193E-03, 2.0464414E-03, 5.2541788E-02, 1.3682959E-04, 1.3454746E-04},
    {57.953, 69.553, 1.0511294E+03, 6.0956091E+01, 1.7156001E+01, -2.5931041E+00, -5.8339803E-02, 1.9954137E-02, -1.5305581E-01, -2.9523967E-03, 1.1340164E-03},
};

constexpr Segment segmentsN[] = {
    {-4.313, 0.0, -5.9610511E+01, -1.5000000E+00, 4.2021322E+01, 4.7244037E+00, -6.1153213E+00, -9.9980337E-01, 1.6385664E-01, -1.4994026E-01, -3.0810372E-02},
    {0.0, 20.613, 3.1534505E+02, 9.8870997E+00, 2.7988676E+01, 1.5417343E+00, -1.4689457E-01, -6.8322712E-03, 6.2600036E-02, -5.1489572E-03, -2.8835863E-04},
    {20.613, 47.513, 1.0340172E+03, 3.7565475E+01, 2.6029492E+01, -6.0783095E-01, -9.7742562E-03, -3.3148813E-06, -2.5351881E-02, -3.8746827E-04, 1.7088177E-06},
};

constexpr Segment segmentsR[] = {
    {-0.226, 1.469, 1.3054315E+02, 8.8333090E-01, 1.2557377E+02, 1.3900275E+02, 3.3035469E+01, -8.5195924E-01, 1.2232896E+00, 3.5603023E-01, 0.0},
    {1.469, 7.461, 5.4188181E+02, 4.9312886E+00, 9.0208190E+01, 6.1762254E+00, -1.2279323E+00, 1.4873153E-02, 8.7670455E-02, -1.2906694E-02, 0.0},
    {7.461, 14.277, 1.0382132E+03, 1.1014763E+01, 7.4669343E+01, 3.4090711E+00, -1.4511205E-01, 6.3077387E-03, 5.6880253E-02, -2.0512736E-03, 0.0},
    {14.277, 21.101, 1.5676133E+03, 1.8397910E+01, 7.1646299E+01, -1.0866763E+00, -2.0968371E+00, -7.6741168E-01, -1.9712341E-02, -2.9903595E-02, -1.0766878E-02},
};

constexpr Segment segmentsS[] = {
    {-0.236, 1.441, 1.3792630E+02, 9.3395024E-01, 1.2761836E+02, 1.1089050E+02, 1.9898457E+01, 9.6152996E-02, 9.6545918E-01, 2.0813850E-01, 0.0},
    {1.441, 6.913, 4.7673468E+02, 4.0037367E+00, 1.0174512E+02, -8.9306371E+00, -4.2942435E+00, 2.0453847E-01, -7.1227776E-02, -4.4618306E-02, 1.6822887E-03},
    {6.913, 12.856, 9.7946589E+02, 9.3508283E+00, 8.7126730E+01, -2.3139202E+00, -3.2682118E-02, 4.6090022E-03, -1.4299790E-02, -1.2289882E-03, 0.0},
    {12.856, 18.693, 1.6010461E+03, 1.6789315E+01, 8.4315871E+01, -1.0185043E+01, -4.6283954E+00, -1.0158749E+00, -1.2877783E-01, -5.5802216E-02, -1.2146518E-02},
};

constexpr Segment segmentsT[] = {
    {-6.18, -4.648, -1.9243000E+02, -5.4798963E+00, 5.9572141E+01, 1.9675733E+00, -7.8176011E+01, -1.0963280E+01, 2.7498092E-01, -1.3768944E+00, -4.5209805E-01},
    {-4.648, 0.0, -6.0000000E+01, -2.1528350E+00, 3.0449332E+01, -1.2946560E+00, -3.0500735E+00, -1.9226856E-01, 6.9877863E-03, -1.0596207E-01, -1.0774995E-02},
    {0.0, 9.288, 1.3500000E+02, 5.9588600E+00, 2.0325591E+01, 3.3013079E+00, 1.2638462E-01, -8.2883695E-04, 1.7595577E-01, 7.9740521E-03, 0.0},
    {9.288, 20.872, 3.0000000E+02, 1.4861780E+01, 1.7214707E+01, -9.3862713E-01, -7.3509066E-02, 2.9576140E-04, -4.8095795E-02, -4.7352054E-03, 0.0},
};

constexpr Curve curves[] = {
    {&coldB, nullptr, {0, 100}, segmentsB, sizeof segmentsB / sizeof *segmentsB}, // B
    {&coldE, &coldEBelowZero, {-40, 100}, segmentsE, sizeof segmentsE / sizeof *segmentsE}, // E
    {&coldJ, nullptr, {-40, 100}, segmentsJ, sizeof segmentsJ / sizeof *segmentsJ}, // J
    {nullptr, nullptr, {int(TypeK::Minimum), int(TypeK::Maximum)}, nullptr, 0}, // K: TypeK over its whole range
    {&coldN, &coldNBelowZero, {-40, 100}, segmentsN, sizeof segmentsN / sizeof *segmentsN}, // N
    {&coldR, nullptr, {-40, 100}, segmentsR, sizeof segmentsR / sizeof *segmentsR}, // R
    {&coldS, nullptr, {-40, 100}, segmentsS, sizeof segmentsS / sizeof *segmentsS}, // S
    {&coldT, &coldTBelowZero, {-40, 100}, segmentsT, sizeof segmentsT / sizeof *segmentsT}, // T
};
static_assert(sizeof curves / sizeof *curves == size_t(ThermocoupleType::Count), "a curve per thermocouple type");

double coldJunctionVoltage(const Curve &curve, double junction) {
    const ColdPiece &piece = curve.belowZero && junction < 0.0 ? *curve.belowZero : *curve.cold;
    const double x = junction - piece.offset, *c = piece.c;
    return c[0] + x * (c[1] + x * (c[2] + x * (c[3] + x * (c[4] + x * (c[5] + x * c[6])))));
}
double segmentTemperature(const Segment &s, double millivolts) {
    const double d = millivolts - s.V0;
    return s.T0 + d * (s.p1 + d * (s.p2 + d * (s.p3 + s.p4 * d))) / (1.0 + d * (s.q1 + d * (s.q2 + s.q3 * d)));
}
} // namespace

const char *Thermocouple::letter(ThermocoupleType type) {
    static constexpr const char *letters[] = {"B", "E", "J", "K", "N", "R", "S", "T"};
    static_assert(sizeof letters / sizeof *letters == size_t(ThermocoupleType::Count), "a letter per thermocouple type");
    return type < ThermocoupleType::Count ? letters[size_t(type)] : "";
}
JunctionWindow Thermocouple::junctionWindow(ThermocoupleType type) { return curves[size_t(type)].window; }

bool Thermocouple::junctionInRange(ThermocoupleType type, double junctionCelsius) {
    const JunctionWindow window = junctionWindow(type);
    return junctionCelsius >= window.from && junctionCelsius <= window.to;
}

bool Thermocouple::convert(double volts, double junctionCelsius, ThermocoupleType type, double &celsius) {
    if (!junctionInRange(type, junctionCelsius)) return false;
    if (type == ThermocoupleType::K) return TypeK::convert(volts, junctionCelsius, celsius);
    const Curve &curve = curves[size_t(type)];
    const double millivolts = volts * 1000 + coldJunctionVoltage(curve, junctionCelsius);
    for (size_t i = 0; i < curve.count; ++i) {
        const Segment &s = curve.segments[i];
        if (!(s.from <= millivolts && millivolts < s.to)) continue;
        celsius = segmentTemperature(s, millivolts);
        return true;
    }
    return false;
}
} // namespace openosc::dmm
