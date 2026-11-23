#pragma once
#include "lvgl.h"
#include "openosc/ui/scope/model.hpp"
#include "openosc/ui/scope/plot_frame.hpp"
#include "openosc/ui/scope/record_gaps.hpp"
#include "openosc/settings/color_value.hpp"

namespace openosc::plot {
// The plot's colours. trigger: FollowSource draws the trigger marks in the source channel's colour;
// tagText: Contrast writes on a coloured tag (zero tags, the active cursor's) in the ink that
// contrasts with that tag (ColorContrast); readout: neutral texts on the plot (scales, record-edge
// and spectrum labels). The Scope appearance's Trigger, TagText and Readout roles (ScopeLook).
struct Palette {
    static constexpr uint32_t FollowSource = 0xffffffffu, Contrast = 0xfffffffeu;
    uint32_t background = 0, grid = 0, channels[2]{}, math = 0, reference = 0xa78bfa, cursor = 0xe2e8f0;
    uint32_t trigger = FollowSource, tagText = Contrast, readout = 0xe2e8f0;
    uint32_t triggerColor(unsigned source) const { return trigger == FollowSource ? channels[source & 1] : trigger; }
    uint32_t tagInk(uint32_t fill) const { return tagText == Contrast ? ColorContrast::ink(fill) : tagText; }
};
class Painter {
public:
    // frame: the whole waveform object; the plot is frame less the PlotFrame margins.
    Painter(lv_layer_t *layer, const lv_area_t &frame, Model &model, const Palette &palette,
            bool inlineScales = true)
        : layer_(layer), frame_(frame), area_(plotArea(frame)), model_(model), palette_(palette), inlineScales_(inlineScales) {}
    void draw();
    static lv_area_t plotArea(const lv_area_t &frame) {
        return {frame.x1 + PlotFrame::Left, frame.y1 + PlotFrame::Top, frame.x2 - PlotFrame::Right, frame.y2 - PlotFrame::Bottom};
    }

private:
    void grid();
    // A channel's trace colour at its brightness (GitHub #86), once a layer: the marks, tags and
    // labels keep the channel's own colour (Palette::channels).
    uint32_t traceColor(unsigned channel) const;
    void recordGaps(const RecordGaps &gaps);      // The window's parts outside the record (record_edges.cpp).
    void recordGapLabels(const RecordGaps &gaps);
    void traces();
    void glow(unsigned channel);
    void density(unsigned channel); // A graded channel's density plane, from its own palette (GitHub #85).
    bool densityShown(unsigned channel) const; // Graded with levels to draw: no trace, no glow over it.
    void spectrum();
    void persistence();
    void markers();
    void cursors();
    void scales();
    void trace(const Trace &trace, uint32_t color, uint8_t opacity, unsigned width, Style style,
               bool center = false);
    void xy(const Point *points, unsigned count, uint32_t color, uint8_t opacity, unsigned width,
            Style style);
    void text(const char *text, int x, int y, uint32_t color, int width = 90,bool solid=false);
    void tag(const char *text, int x, int y, uint32_t foreground, uint32_t background);
    void fill(const lv_area_t &box, uint32_t color);
    void line(int x1, int y1, int x2, int y2, uint32_t color, uint8_t opacity = 255, bool dashed = false, unsigned width = 1);
    void cursorLine(CursorLine name, const char *label, int x1, int y1, int x2, int y2, int tagX, int tagY, uint32_t color);
    void triangle(Point a, Point b, Point c, uint32_t color);
    void zeroTag(unsigned channel, int zero, int centre, int bottom);
    lv_layer_t *layer_;
    lv_area_t frame_, area_; // area_: the plot; drawing coordinates are relative to its corner.
    Model &model_;
    const Palette &palette_;
    bool inlineScales_;
};
class Path {
public:
    Path(lv_layer_t *layer, const lv_area_t &area, uint32_t color, uint8_t opacity, unsigned width,
         Statistics &statistics);
    ~Path() { flush(false); }
    void vertex(int x, int y);
    void gap();

private:
    void flush(bool keepLast);
    lv_layer_t *layer_;
    const lv_area_t &area_;
    lv_draw_line_dsc_t descriptor_;
    lv_point_precise_t points_[64];
    unsigned count_ = 0;
    Statistics &statistics_;
};
} // namespace openosc::plot
