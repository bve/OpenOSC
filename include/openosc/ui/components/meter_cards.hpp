#pragma once
#include "lvgl.h"
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/ui/components/function_symbols.hpp"
#include "openosc/ui/components/parameter_bar.hpp"

namespace openosc::ui {
// The DMM's F1..F4 cards as signs instead of words: each card's row shows every function its key
// selects (F1 V DC/AC; F2 Ω, continuity, diode, capacitor; F3 A DC/AC; F4 mV DC/AC and thermocouple
// on V/Ω, mA DC/AC on mA), the decoded function in the highlight colour, the others muted (on the selected
// card's accent fill: its ink, the others faded). The cards themselves (fill, focus, the value
// line) stay ParameterBar's, shared with Scope and GEN.
class MeterCards {
public:
    static constexpr int SignHeight = 14, Inset = 4, Gap = 3, MaximumSigns = 4;
    void create(ParameterBar &bar);
    void visible(bool value);
    void show(dmm::Function active, bool beeper, bool currentInput, uint32_t highlight, uint32_t muted);
    // The signs of card `index` (tests: what is drawn where).
    unsigned signs(unsigned index, FunctionSymbols::Symbol (&output)[MaximumSigns]) const;
    lv_obj_t *row(unsigned index) const { return index < ParameterBar::Count ? rows_[index] : nullptr; }
    FunctionSymbols::Symbol active() const { return active_; }

private:
    static void draw(lv_event_t *event);
    lv_obj_t *rows_[ParameterBar::Count]{};
    ParameterBar *bar_ = nullptr; // Shows and hides the rows (under a context: at its release).
    FunctionSymbols::Symbol active_ = FunctionSymbols::Symbol::Unknown;
    bool currentInput_ = false;
    uint32_t highlight_ = 0, muted_ = 0;
};
} // namespace openosc::ui
