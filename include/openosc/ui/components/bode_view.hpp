#pragma once
#include "openosc/acquisition/bode.hpp"
#include "openosc/acquisition/bode_analysis.hpp"
#include "openosc/generator/waveform.hpp"
#include "openosc/settings/theme.hpp"
#include "lvgl.h"

namespace openosc::ui {
// The Bode result (GitHub #72) between the status bar and the soft keys, in two forms over one cursor:
// - Plot: the gain in dB above (scaled to the measured span in 1-2-5 steps, its 0 dB line marked and
//   labelled first, the labels centred on their lines), the phase in degrees below (-180..180, its 0 line
//   marked), on one log frequency axis with its decades and, where no decade label is near, the sweep's
//   ends; lines join neighbouring points (an invalid point is a gap in both, a weak CH2 a gap in the
//   phase); the two cursors (the active one solid, the other dashed, each with its letter), the markers.
// - Table: PageRows points a page, the active cursor's row highlighted, the other's shaded, each with its
//   letter: index, frequency, gain, phase and a note (why a point is invalid, a weak CH2, fewer averages
//   than asked, the markers on it); below, the active point's stimulus and output (CH1/CH2 Vpp), V/div.
// - Results: the run's peak, -3 dB bounds and bandwidth, gain and phase margins with their frequencies
//   (scope::BodeResults, "--" where none qualifies), the run as it ran and the rules.
// Plot and Table: a header (what is measured, the sweep's state and progress), the active cursor's point
// (its letter, values or why it has none, a weak CH2's phase "--", the marker Up/Down went to), and the
// cursors' difference B - A in Hz, dB and degrees ("--" where either point has no such value), with MENU
// named as the key that swaps the active cursor.
enum class BodeDrive : uint8_t { FromGen, Dedicated, Low, High, Custom }; // What the sweep plays (setup).
struct BodeViewData {
    const scope::BodePoint *points = nullptr;
    unsigned count = 0, measured = 0;
    scope::BodeCursor cursor;      // The active cursor (its point, the named marker, Table or Plot).
    unsigned other = 0;            // The other cursor's point.
    bool second = false;           // The active cursor is B.
    bool results = false;          // The Results page.
    scope::BodeMarkers markers{0, 0, 0, 0, 0};
    const scope::BodeResults *analysis = nullptr;     // The run's results,
    const scope::BodeSettings *run = nullptr;         // its settings
    const generator::Settings *excitation = nullptr;  // and the drive it played.
    BodeDrive drive = BodeDrive::FromGen;
    const char *status = "";       // "Sweep 12/31", "Done: 29/31 valid", ...
    double startHz = 1, stopHz = 10;
    bool running = false;          // The progress bar under the header.
};
class BodeView {
public:
    static constexpr int Top = 20, Height = 188, Left = 36, Right = 314, Line = 14;
    // Rows of text (relative to Top): the header, the cursor (Plot) or the columns' titles (Table), the
    // cursors' difference (Plot); the progress bar between the first two.
    static constexpr int RowA = 1, Progress = 15, RowB = 17, RowC = 31;
    static constexpr int GainTop = 46, GainBottom = 104, PhaseTop = 112, PhaseBottom = 170;
    static constexpr int TableTop = 31, FooterA = 160, FooterB = 174;
    static constexpr unsigned PageRows = scope::BodeCursor::PageRows;
    static_assert(PhaseBottom + 1 + Line <= Height, "Decade labels inside the plot's height");
    static_assert(RowC + Line <= GainTop, "The difference's row above the gain panel");
    static_assert(TableTop + int(PageRows) * Line <= FooterA - 2 && FooterB + Line <= Height, "A page and its footer fit");
    void create(lv_obj_t *parent, const Theme &theme);
    void clear() { if (root_) lv_obj_delete(root_); root_ = nullptr; }
    void colors(const Theme &theme);
    void show(const BodeViewData &data);
    void hide() { if (root_) lv_obj_set_hidden(root_, true); }
    bool visible() const { return root_ && !lv_obj_is_hidden(root_); }
    lv_obj_t *root() const { return root_; }
    const BodeViewData &data() const { return data_; } // As last shown (tests, the soft keys).
    // The x of a frequency on the plot (tests).
    static int x(double hz, double startHz, double stopHz);
    // Why an invalid point has no values: in a sentence (the cursor) and in a word or two (the table).
    static const char *reason(uint16_t flags);
    static const char *note(uint16_t flags);
    static const char *driveName(BodeDrive drive); // "From GEN", "Dedicated", ...

private:
    lv_obj_t *root_ = nullptr;
    BodeViewData data_;
    uint32_t background_ = 0, foreground_ = 0, grid_ = 0, gain_ = 0, phase_ = 0, accent_ = 0, muted_ = 0;
    // The parts, each its own frame (noinline): only one chain of their buffers on the stack at a time
    // (LVGL draws on the main stack, the ZT703S's margin is a few hundred bytes).
    static void draw(lv_event_t *event);
    [[gnu::noinline]] void header(lv_layer_t *layer) const;
    [[gnu::noinline]] void readout(lv_layer_t *layer, int y) const;
    [[gnu::noinline]] void delta(lv_layer_t *layer, int x, int y) const;
    [[gnu::noinline]] void plot(lv_layer_t *layer) const;
    [[gnu::noinline]] void table(lv_layer_t *layer) const;
    [[gnu::noinline]] void footer(lv_layer_t *layer, int y) const;
    [[gnu::noinline]] void results(lv_layer_t *layer) const;
    [[gnu::noinline]] void runLines(lv_layer_t *layer, int y) const;
    unsigned pointA() const { return data_.second ? data_.other : data_.cursor.point; }
    unsigned pointB() const { return data_.second ? data_.cursor.point : data_.other; }
};
#ifdef OPENOSC_APPLICATION
// The application's one view (ApplicationView::bode()): in ITCM (.bode, beside the session; zeroed at
// reset) and built in place on first use, so its copy of the data is on no stack (the controller and
// its view are main()'s frame, which the ZT706S's DTCM budget leaves little room in).
BodeView &bodeView();
#endif
} // namespace openosc::ui
