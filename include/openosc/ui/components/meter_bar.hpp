#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/measurement/dmm/bar.hpp"
#include "openosc/ui/components/meter_style.hpp"
#include <algorithm>

namespace openosc::ui {
// Segmented bar in the style of a handheld DMM: zero at the left, magnitude to the right, a
// separate polarity sign and the reading's scale (BarReading::rangeScale: 25000 counts on the ZT703S,
// 66000 on the ZT706S, a range's own end where it comes first, 10000 for capacitance, 0-3.3 V for the diode) numbered in the active range's unit as the
// analog dial numbers it (GitHub #134, MeterText::scaleNumber): 2.5 V reads 0, 0.5 ... 2.5 V, the unit
// after the last number. Before a reading gives the decimals (no packet yet, a blank on another
// scale), the numbers are thousands of counts without a unit; a blank or stale reading on the same
// scale keeps the range's numbers. Temperature uses signed degree bands. Labels change only with the
// scale, the decimals or the unit (no text work per packet).
// GitHub #139: a notch marks each number's value on the segments, where valueX() places it; the
// number is centred on it, the first starts and the last ends there. Each segment owns 1/Segments of
// the scale over its cell (segment and gap), so the fill reaches a value's notch as it reaches the
// value. A notch takes the numbers' colour over unlit segments and gaps and the face's colour over
// lit ones (a cut): lighter or darker by the theme, never hidden by the fill. The two end notches are
// two pixels wide, always in the numbers' colour. Every notch also protrudes NotchDepth rows below the
// segments in the numbers' colour on any fill, clear of the numbers' ink. Notches move only with the
// scale; the fill alone never moves them or the numbers.
class MeterBar {
public:
    void create(lv_obj_t *parent);
    void update(const dmm::Result &, DataStatus, uint32_t accent,
                const dmm::RelativeReading *relative = nullptr, dmm::TemperatureAxis *axis = nullptr);
    void palette(const MeterPalette &palette); // Unlit segments, scale text, the face and the OL colour.
    void visible(bool value);                   // The analog dial replaces the bar.
    void top(int y);                            // Its rows from y (sign, segments, numbers); default Top.
    // The unit after the last number (Measurement > Bar scale unit, default On): Off leaves the last
    // number alone, ending at its notch. Relabels at once, without a new reading.
    void unit(bool shown);
    static constexpr int Top = 63;
    // Tests: a number's label (0..labels - 1 of the scale on show).
    lv_obj_t *tick(int index) const { return index >= 0 && index < dmm::MaxScaleLabels ? ticks_[index] : nullptr; }
    lv_obj_t *segments() const { return bar_; } // Tests: the segments' area.
    static constexpr int Segments = 48, Pitch = 6, SegmentWidth = 5, BarX = 8;
    // The segments' rows, then the notches' protrusion below them: the bar's 10 rows down to the numbers'
    // label box, its top rows blank above the glyphs' ink (a gap of two rows in the scale font).
    static constexpr int SegmentHeight = 8, NotchDepth = 2, Height = SegmentHeight + NotchDepth;
    static constexpr int Cells = Segments * Pitch, Width = Cells - (Pitch - SegmentWidth); // 288 units, 287 px.
    // The column (in the parent, from BarX) of the value numerator / denominator of the full scale: the
    // nearest pixel to its place, the full scale on the last segment's last pixel.
    static constexpr int valueX(int numerator, int denominator) {
        return BarX + std::min(Width - 1, (2 * numerator * Cells + denominator) / (2 * denominator));
    }
    static constexpr int tickX(int index, int labels) { return valueX(index, labels - 1); } // A number's notch.
private:
    static constexpr int StateX = BarX + 118, StateWidth = 52; // The state caption over the scale numbers.
    static constexpr int LabelGap = 2; // Between neighbouring numbers and around the state caption.
    static void draw(lv_event_t *event);
    void showTicks(); // Each number, unless the state caption (NO DATA, STALE, OL, INVALID) covers it.
    void scale(dmm::CountScale counts, unsigned decimals, const char *unit);
    void label(); // The numbers of scale_, decimals_ and unit_ on their notches.
    lv_obj_t *bar_ = nullptr, *state_ = nullptr, *sign_ = nullptr, *ticks_[dmm::MaxScaleLabels]{};
    dmm::CountScale scale_ = dmm::BoardScale;
    uint8_t decimals_ = 255; // The numbers' decimals (255: thousands of counts) and unit, as labelled.
    char unit_[8]{};
    uint32_t off_ = 0x243449, overload_ = 0xf87171, muted_ = 0x94a3b8, face_ = 0x0b1120;
    int lit_ = 0;
    uint32_t color_ = 0;
    DataStatus status_ = DataStatus::NoData;
    bool negative_ = false, initialized_ = false, visible_ = true, unitShown_ = true;
    bool covered_[dmm::MaxScaleLabels]{}; // Numbers under the state caption.
};
}
