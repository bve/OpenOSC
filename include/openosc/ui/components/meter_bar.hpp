#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/measurement/dmm/bar.hpp"
#include "openosc/ui/components/meter_style.hpp"

namespace openosc::ui {
// Segmented bar in the style of a handheld DMM: zero at the left, magnitude to the right, a
// separate polarity sign and the reading's scale (dmm::scaleFor: 25000 counts on the ZT703S, 66000 on
// the ZT706S, 10000 for capacitance, 0-3.3 V for the diode) numbered in the active range's unit as the
// analog dial numbers it (GitHub #134, MeterText::scaleNumber): 2.5 V reads 0, 0.5 ... 2.5 V, the unit
// after the last number. Before a reading gives the decimals (no packet yet, a blank on another
// scale), the numbers are thousands of counts without a unit; a blank or stale reading on the same
// scale keeps the range's numbers. Temperature uses signed degree bands. Labels change only with the
// scale, the decimals or the unit (no text work per packet).
class MeterBar {
public:
    void create(lv_obj_t *parent);
    void update(const dmm::Result &, DataStatus, uint32_t accent,
                const dmm::RelativeReading *relative = nullptr, dmm::TemperatureAxis *axis = nullptr);
    void palette(const MeterPalette &palette); // Unlit segments, scale text and the OL colour.
    void visible(bool value);                   // The analog dial replaces the bar.
    void top(int y);                            // Its rows from y (sign, segments, numbers); default Top.
    static constexpr int Top = 63;
    // Tests: a number's label (0..labels - 1 of the scale on show).
    lv_obj_t *tick(int index) const { return index >= 0 && index < dmm::MaxScaleLabels ? ticks_[index] : nullptr; }
    static constexpr int tickX(int index, int labels) { return BarX + index * Segments / (labels - 1) * Pitch; }
private:
    static constexpr int Segments = 48, Pitch = 6, SegmentWidth = 5, Height = 9, BarX = 8;
    static constexpr int StateX = BarX + 118, StateWidth = 52; // The state caption over the scale numbers.
    static constexpr int LabelGap = 2; // Between neighbouring numbers and around the state caption.
    static void draw(lv_event_t *event);
    void showTicks(); // Each number, unless the state caption (NO DATA, STALE, OL, INVALID) covers it.
    void scale(dmm::CountScale counts, unsigned decimals, const char *unit);
    lv_obj_t *bar_ = nullptr, *state_ = nullptr, *sign_ = nullptr, *ticks_[dmm::MaxScaleLabels]{};
    dmm::CountScale scale_ = dmm::BoardScale;
    uint8_t decimals_ = 255; // The numbers' decimals (255: thousands of counts) and unit, as labelled.
    char unit_[8]{};
    uint32_t off_ = 0x243449, overload_ = 0xf87171, muted_ = 0x94a3b8;
    int lit_ = 0;
    uint32_t color_ = 0;
    DataStatus status_ = DataStatus::NoData;
    bool negative_ = false, initialized_ = false, visible_ = true;
    bool covered_[dmm::MaxScaleLabels]{}; // Numbers under the state caption.
};
}
