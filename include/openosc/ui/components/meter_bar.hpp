#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/measurement/dmm/bar.hpp"
#include "openosc/ui/components/meter_style.hpp"

namespace openosc::ui {
// Segmented bar in the style of a handheld DMM: zero at the left, magnitude to the right, a
// separate polarity sign and the reading's scale in thousands of counts (dmm::scaleFor: 0-25 on the
// ZT703S, 0-66 on the ZT706S, 0-10 for capacitance on both). Temperature uses signed degree bands.
class MeterBar {
public:
    void create(lv_obj_t *parent);
    void update(const dmm::Result &, DataStatus, uint32_t accent,
                const dmm::RelativeReading *relative = nullptr, dmm::TemperatureAxis *axis = nullptr);
    void palette(const MeterPalette &palette); // Unlit segments, scale text and the OL colour.
    void visible(bool value);                   // The analog dial replaces the bar.
    void top(int y);                            // Its rows from y (sign, segments, numbers); default Top.
    static constexpr int Top = 63;
private:
    static constexpr int Segments = 48, Pitch = 6, SegmentWidth = 5, Height = 9, BarX = 8;
    static constexpr int TickWidth = 16, StateX = BarX + 118, StateWidth = 52; // The state caption over the scale numbers.
    static void draw(lv_event_t *event);
    void showTicks(); // Each number, unless the state caption (NO DATA, STALE, OL, INVALID) covers it.
    void scale(dmm::CountScale counts, bool temperature = false);
    lv_obj_t *bar_ = nullptr, *state_ = nullptr, *sign_ = nullptr, *ticks_[dmm::MaxScaleLabels]{};
    dmm::CountScale scale_ = dmm::BoardScale;
    uint32_t off_ = 0x243449, overload_ = 0xf87171, muted_ = 0x94a3b8;
    int lit_ = 0;
    uint32_t color_ = 0;
    DataStatus status_ = DataStatus::NoData;
    bool negative_ = false, initialized_ = false, visible_ = true;
    bool covered_[dmm::MaxScaleLabels]{}; // Numbers under the state caption.
};
}
