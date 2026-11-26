#pragma once
#include "openosc/data/trend_window.hpp"
#include "openosc/ui/components/menu_palette.hpp"
#include "lvgl.h"

namespace openosc::ui {
// What the saved-session view shows (GitHub #144): the file's summary line, the window's trend, the
// cursor's column and where the window lies in the session. trend null: nothing reduced yet.
struct SessionTrendData {
    const dmm::TrendWindow *trend = nullptr;
    const dmm::SessionInfo *info = nullptr;
    uint64_t firstUs = 0, lastUs = 0; // The session's extent.
    unsigned cursor = 0, zoom = 1;
    uint32_t records = 0, invalid = 0;
    const char *status = nullptr; // Over the plot: "Reading 40%", an error.
};
// The trend of a saved session in the menu box, drawn by itself (no chart widget, no copy of the data):
// - a line: mode, function, duration, records (and lines skipped as corrupt);
// - the plot, one column a pixel: each column's min..max as a vertical stroke joined to its neighbours
//   (a spike or a drop of one reading stays visible at any zoom), nothing where the session has no
//   value (a gap is a mark on the baseline, OL one at the top), the auto-scaled limits at the left;
// - the window's start and end times, the zoom and a bar of where the window is in the session;
// - the cursor (a dashed line) and its column: time, min, max and average.
class SessionTrendView {
public:
    static constexpr int Top = 24, Width = 304, Height = 156;
    static constexpr int PlotLeft = 44, PlotTop = 16, PlotHeight = 80, PlotBottom = PlotTop + PlotHeight - 1;
    static constexpr int PlotRight = PlotLeft + int(dmm::TrendWindow::Columns) - 1;
    static_assert(PlotRight < Width - 2, "The plot inside the menu box");
    void create(lv_obj_t *parent, const MenuPalette &palette); // A child of the menu box (rebuilt with it).
    void show(const SessionTrendData &data);
    void destroy() { if (root_) lv_obj_delete(root_); root_ = nullptr; } // Before this object goes.
    lv_obj_t *root() const { return root_; }
    const SessionTrendData &data() const { return data_; }
    // The value with its unit as the view writes it ("1.2345 V", "21.5 °C"); empty when not finite.
    static void value(double si, Quantity quantity, char *output, size_t capacity);
    static void time(uint64_t us, bool fine, char *output, size_t capacity); // "1:02:03", "12:34.5"
    // The plot's y of a value within [low, high] (high at PlotTop).
    static int y(double value, double low, double high);

private:
    static void draw(lv_event_t *event);
    [[gnu::noinline]] void summary(lv_layer_t *layer, int x0, int y0) const;
    [[gnu::noinline]] void plot(lv_layer_t *layer, int x0, int y0, double low, double high) const;
    [[gnu::noinline]] void axis(lv_layer_t *layer, int x0, int y0, double low, double high) const;
    [[gnu::noinline]] void readout(lv_layer_t *layer, int x0, int y0) const;
    lv_obj_t *root_ = nullptr;
    SessionTrendData data_;
    MenuPalette palette_;
};
} // namespace openosc::ui
