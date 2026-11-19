#pragma once
#include "lvgl.h"
#include "openosc/measurement/dmm/function.hpp"

namespace openosc::ui {
// The DMM function signs as code vectors, one geometry for every size: the function row's sign
// (FunctionIcon, 26 px) and the DMM cards (14 px). Ω, the sounder (continuity), the diode and the
// capacitor are the Tabler Icons outlines omega, volume-2, circuit-diode and circuit-capacitor
// (MIT, pinned in docs/ui/icons/tabler) on their 24x24 grid, scaled; V, mV, A and mA are the UI
// font's letters followed by the DC mark (a line over three dashes) or the AC mark (a sine).
class FunctionSymbols {
public:
    enum class Symbol : uint8_t { VoltsDc, VoltsAc, MillivoltsDc, MillivoltsAc, AmpsDc, AmpsAc, MilliampsDc, MilliampsAc,
                                  Ohm, Continuity, Diode, DiodeBeeper, Capacitor, Unknown };
    static Symbol of(dmm::Function function, bool beeper);
    // bare: beside the unit (the function row), only what the unit doesn't say: "DC"/"AC", a resistor for Ω.
    static int width(Symbol symbol, int height, bool bare = false);
    static void draw(lv_layer_t *layer, Symbol symbol, int x, int y, int height, lv_color_t color, bool bare = false);
};
} // namespace openosc::ui
