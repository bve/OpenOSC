#pragma once
#include "lvgl.h"
#include "openosc/measurement/dmm/bar.hpp"
#include "openosc/ui/components/meter_style.hpp"
#include "openosc/ui/components/needle_motion.hpp"

namespace openosc::ui {
// A moving-coil panel meter: a wide arc whose radius follows the face's width (the pivot is below
// the face), the numbers above it, the reading's scale (thousands of counts of the range,
// dmm::scaleFor: 0-25 on the ZT703S, 0-66 on the ZT706S, 0-10 for capacitance on both), a needle with
// NeedleMotion inertia. Like the bar, the needle shows the magnitude and a separate minus at the lower
// left, before the scale in reading order, marks a negative reading; OL pegs it right in the alert
// colour; no data lets it fall back to zero; stale and HOLD keep it where it is and say so (after the minus).
// The input jacks (and RMS) are its marking, as on a panel meter's face: right-aligned in the top
// right corner, which the arc, the ticks and the needle never reach and the numbers leave free.
// service() advances the needle by the real elapsed time and invalidates only the needle's
// old and new strip, at most once per FrameMs; hidden or settled, it costs nothing.
class AnalogDial {
public:
    static constexpr int HalfAngle = 46, TopMargin = 22;   // Degrees each side of vertical; arc top below the face's top.
    static constexpr int SideMargin = 18, LabelOffset = 11; // The arc's ends from the sides; numbers above the arc.
    static constexpr int SignX = SideMargin + 12, SignWidth = 24; // The minus at the bottom left, inside the arc's end; the caption after it.
    static constexpr int MarkingY = 6, MarkingHeight = 15, MarkingFont = 12; // The marking's row in the top right corner.
    static constexpr uint32_t FrameMs = 13;                 // Two LTDC scans (6.5 ms each): even needle steps.
    enum class State : uint8_t { Live, Overload, NoData, Stale };
    lv_obj_t *create(lv_obj_t *parent);
    void place(int x, int y, int width, int height);
    void visible(bool value);
    bool shown() const { return object_ && !lv_obj_is_hidden(object_); }
    lv_obj_t *object() const { return object_; } // Tests: its area.
    // The dial's colours and line widths (MeterLook: background, arc, ticks, numbers, needle).
    void palette(const MeterLook &look);
    void reading(const dmm::BarReading &value, bool held);
    // The scale in the reading's own unit: full scale is `counts.fullScale` counts at `decimals` (255:
    // unknown, the ticks are then labelled in thousands of counts with an "x1000 counts" legend).
    void scale(unsigned decimals, const char *unit, dmm::CountScale counts = dmm::BoardScale);
    dmm::CountScale counts() const { return counts_; } // A tick per 1000 counts, a label every major thousand.
    const char *label(unsigned major) const { return labels_[major]; } // 0..counts().labels() - 1
    const char *legend() const { return legend_; }
    // The jacks and RMS ("RMS COM+10A"); "" for none.
    void marking(const char *text);
    const char *marking() const { return marking_; }
    lv_area_t markingArea() const;           // Its text's extent on the screen (empty text: zero width).
    lv_area_t numberArea(int major) const;   // The box a scale number is centred in, on the screen.
    bool service(uint32_t now); // True if a redraw was requested.
    const NeedleMotion &motion() const { return motion_; }
    State state() const { return state_; }
    // The caption at the bottom left (OL, NO VALUE, STALE..., else HOLD) and whether it is in the alert colour.
    const char *caption() const { return *caption_ ? caption_ : held_ ? "HOLD" : ""; }
    bool captionAlert() const { return alert_ || held_; }
    bool negative() const { return negative_; }
    // The needle's angle (degrees clockwise from vertical) at a scale fraction.
    static float angle(float fraction) { return (fraction * 2 - 1) * HalfAngle; }

private:
    struct Geometry { float px, py, radius; };
    Geometry geometry() const;
    lv_area_t needleArea(float fraction) const; // Screen coordinates.
    static void draw(lv_event_t *event);
    void paint(lv_layer_t *layer) const;
    lv_obj_t *object_ = nullptr;
    NeedleMotion motion_;
    MeterLook look_{};
    State state_ = State::NoData;
    const char *caption_ = "NO DATA"; // MeterText::caption and ::alert of the reading's status.
    bool alert_ = false;
    bool negative_ = false, held_ = false;
    float drawn_ = 0;     // The position last painted.
    unsigned decimals_ = 256; // Nothing labelled yet.
    dmm::CountScale counts_ = dmm::BoardScale;
    char labels_[dmm::MaxScaleLabels][16]{}, legend_[16]{}, marking_[24]{};
    uint32_t drawnAt_ = 0;
};
} // namespace openosc::ui
