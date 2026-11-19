#pragma once
#include "lvgl.h"
#include "openosc/acquisition/frame.hpp"
#include "openosc/settings/theme.hpp"
#include "openosc/ui/scope/painter.hpp"
#include "openosc/ui/scope/column_cache.hpp"

namespace openosc {
class WaveformWidget {
public:
    explicit WaveformWidget(plot::Storage &storage = plot::applicationStorage(),
                            scope::SpectrumStorage &spectrum = plot::applicationSpectrum())
        : model_(storage, spectrum) {}
    WaveformWidget(const WaveformWidget &) = delete;
    WaveformWidget &operator=(const WaveformWidget &) = delete;
    lv_obj_t *create(lv_obj_t *parent);
    void colors(const Theme &theme);
    // The Scope appearance's roles the theme has no field for (plot::Palette: FollowSource and
    // Contrast keep their automatic meaning); repaints. Never touches the record or the options.
    void roles(uint32_t trigger, uint32_t tagText, uint32_t readout);
    const plot::Palette &palette() const { return palette_; } // The colours in force (tests).
    void setFrame(const scope::FrameView &frame);
    bool configure(const plot::Options &options);
    const plot::Options &options() const { return model_.options(); }
    const plot::Model &model() const { return model_; }
    bool needsFrame() const { return model_.needsFrame(); }
    void clear();
    void held(bool value);
    void trigger(scope::Channel channel, double codes);
    bool cursorLines(const plot::CursorOptions &cursors);
    void cursorFocus(plot::CursorLine line);
    // A one-line note in the top right corner of the plot; empty hides it.
    void note(const char *text, uint32_t color);
    // The frequency counter overlay in the top left corner; empty hides it.
    void counter(const char *text, uint32_t color);
    const char *note() const { return note_ && !lv_obj_is_hidden(note_) ? lv_label_get_text(note_) : ""; } // On show.
    void service(uint32_t now);
    bool captureReference(unsigned channel);
    void clearReference();
    void clearHistory();
    void inlineScales(bool enabled) { inlineScales_ = enabled; }
    // The plot inside the object (PlotFrame margins), in screen coordinates.
    lv_area_t plotArea() const;
    // Columns reduced ahead of the projection (ColumnCache); used when they match the frame.
    void columnCache(const plot::ColumnCache *cache) { cache_ = cache; }

private:
    static void draw(lv_event_t *event);
    // LVGL's cover check: the painter fills the whole frame opaque on every draw, so an area wholly
    // inside the object, drawn at full opacity without radius, transform or blending, needs nothing
    // under it (its parents' backgrounds are skipped). Anything else keeps LVGL's own answer.
    static void cover(lv_event_t *event);
    static void geometry(lv_event_t *event);
    lv_obj_t *object_ = nullptr, *note_ = nullptr, *counter_ = nullptr;
    bool noteLow_ = false; // The note in the plot's bottom left: Y-T+FFT has its header on the top line.
    plot::Model model_;
    plot::Palette palette_;
    uint32_t fadeAt_ = 0, frozenAt_ = 0;
    bool held_ = false, inlineScales_ = true;
    const plot::ColumnCache *cache_ = nullptr;
};
} // namespace openosc
