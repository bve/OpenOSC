#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/components/status_bar_layout.hpp"
#include "openosc/ui/scope/model.hpp"
#include "openosc/ui/scope/channel_view.hpp"

namespace openosc::ui {
// What the status bar's record map shows of the frame on show, in pixels of the map's inside.
// The outline is the whole record. `window` is the part of it on screen (in X-Y, the samples
// plotted), absent when the view lies wholly before or after the record; it is never widened to
// its 2-pixel minimum unless it really overlaps the record. `analyzed` is what the FFT reads in
// the spectrum views, where pan and zoom act on frequency and no time window is drawn. Everything
// here comes from the frame and its projection, never from the request staged for the next RUN.
struct OverviewGeometry {
    static constexpr int None = -1, MinimumSpan = 2;
    uint32_t points = 0; // Points of every channel the frame holds: its record memory in use.
    uint32_t budget = 0; // The record budget the frame was captured for (FrameMetadata::budgetK); 0: none.
    bool budgetShort = false; // Its plan shorter than the budget (FrameMetadata::budgetShort).
    int window = None, windowWidth = 0, analyzed = None, analyzedWidth = 0, trigger = None;
    bool before = false, after = false; // The view lies wholly before the record's start or past its end.
    static OverviewGeometry calculate(const scope::FrameMetadata &metadata, const plot::Viewport &viewport, plot::Mode mode,
                                      uint32_t analyzedPoints, int inside);
    static OverviewGeometry calculate(const plot::Model &model, int inside);
    bool operator==(const OverviewGeometry &o) const {
        return points == o.points && budget == o.budget && budgetShort == o.budgetShort && window == o.window && windowWidth == o.windowWidth && analyzed == o.analyzed &&
               analyzedWidth == o.analyzedWidth && trigger == o.trigger && before == o.before && after == o.after;
    }
    bool operator!=(const OverviewGeometry &o) const { return !(*this == o); }
};
// The record map at the right of the status bar: the caption names the record budget the frame was
// captured for ("512K"), "<512K" when the capture plan reserves part of it (the DMA headroom the fastest
// rates keep: GitHub #131, #138: FrameMetadata::budgetShort), the same for every record of that plan, held
// or not, whatever length each record ends with (a freeze in flight varies it by a few points): "<" does not
// flicker in RUN. Without a budget (Roll, a loaded record), the frame's own
// total. The exact count is Acquisition's Captured row (and WAV:PRE?). The strip shows which part is
// on screen and where the trigger lies. It follows HOLD pan and zoom; it is not a capture progress bar.
// captionText(): that text for a geometry ("--" with no frame).
class RecordOverview {
public:
    using Layout = StatusBarLayout;
    static constexpr int Height = 12, Inside = Layout::OverviewMap.width - 2, InsideHeight = Height - 2;
    // Spectrum views: the FFT's span is a bracket below; the overlay's time window keeps the rows above it.
    static constexpr int SplitWindow = 5, AnalyzedTop = 6, AnalyzedRows = 3;
    static constexpr int Top = 2, MapTop = 2; // The T head rises above the strip, in the bar's top rows.
    enum Part : unsigned { Window, Analyzed, TriggerStem, TriggerHead, Before, After, Parts };
    void clear();
    void create(lv_obj_t *parent, const Theme &theme);
    // The colours of `theme` on the existing objects (a Scope appearance); the next update() repaints.
    void palette(const Theme &theme);
    void update(const plot::Model &model, const plot::ChannelView &view = {}); // view: the FFT bracket's colour slot.
    static void captionText(const OverviewGeometry &geometry, char *output, size_t capacity);
    void visible(bool value) { if (root_) lv_obj_set_hidden(root_, !value); }
    const OverviewGeometry &geometry() const { return shown_; }
    lv_obj_t *root() const { return root_; }
    lv_obj_t *caption() const { return caption_; }
    lv_obj_t *part(Part index) const { return index < Parts ? parts_[index] : nullptr; } // For tests.
private:
    void show(const OverviewGeometry &geometry, bool split, uint32_t analyzedColor);
    lv_obj_t *root_ = nullptr, *caption_ = nullptr, *map_ = nullptr;
    lv_obj_t *parts_[Parts]{};
    OverviewGeometry shown_;
    uint32_t channels_[2]{}, math_ = 0, analyzedColor_ = 0;
    bool split_ = false, drawn_ = false;
};
} // namespace openosc::ui
