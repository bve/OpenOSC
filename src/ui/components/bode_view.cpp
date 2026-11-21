#include "openosc/ui/components/bode_view.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/acquisition/ranges.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/data/decimal.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#ifdef OPENOSC_APPLICATION
#include <new>
#endif

namespace openosc::ui {
#ifdef OPENOSC_APPLICATION
namespace {
alignas(8) __attribute__((section(".bode"))) unsigned char viewStorage[sizeof(BodeView)];
BodeView *viewInstance = nullptr;
} // namespace
BodeView &bodeView() {
    if (!viewInstance) viewInstance = new (viewStorage) BodeView();
    return *viewInstance;
}
#endif
namespace {
constexpr const char *Degree = "\xc2\xb0";
constexpr const char *NoValue = "--"; // ui_12 has no em dash.
constexpr int Width = 320, Margin = 4;
using Point = scope::BodePoint;
using Markers = scope::BodeMarkers;
void text(lv_layer_t *layer, const char *s, int x, int y, int width, uint32_t color, lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    lv_draw_label_dsc_t d;
    lv_draw_label_dsc_init(&d);
    d.font = Elements::font(12);
    d.color = lv_color_hex(color);
    d.text = s; d.text_local = 1; d.align = align;
    lv_area_t a{lv_coord_t(x), lv_coord_t(y), lv_coord_t(x + width - 1), lv_coord_t(y + 13)};
    lv_draw_label(layer, &d, &a);
}
int width(const char *s) { return Elements::textWidth(s, 12); }
void line(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color, int width = 1) {
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(color); d.width = width;
    d.p1 = {lv_value_precise_t(x1), lv_value_precise_t(y1)}; d.p2 = {lv_value_precise_t(x2), lv_value_precise_t(y2)};
    lv_draw_line(layer, &d);
}
void dashed(lv_layer_t *layer, int x, int y1, int y2, uint32_t color) { // A vertical dashed line.
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = lv_color_hex(color); d.width = 1; d.dash_width = 3; d.dash_gap = 3;
    d.p1 = {lv_value_precise_t(x), lv_value_precise_t(y1)}; d.p2 = {lv_value_precise_t(x), lv_value_precise_t(y2)};
    lv_draw_line(layer, &d);
}
void signedFixed(double value, unsigned decimals, char *out, size_t capacity) { // "+1.50", "-0.20", "0.00".
    char digits[14]; // With its sign: at most 15 bytes, the callers' 16.
    Decimal::fixed(value, decimals, digits, sizeof digits);
    bool zero = true; // As printed: "0.00" has no sign.
    for (const char *c = digits; *c; ++c) zero = zero && (*c == '0' || *c == '.' || *c == '-');
    std::snprintf(out, capacity, "%s%s", value > 0 && !zero ? "+" : "", zero && digits[0] == '-' ? digits + 1 : digits);
}
void box(lv_layer_t *layer, int x1, int y1, int x2, int y2, uint32_t color) {
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = lv_color_hex(color);
    lv_area_t a{lv_coord_t(x1), lv_coord_t(y1), lv_coord_t(x2), lv_coord_t(y2)};
    lv_draw_rect(layer, &d, &a);
}
void frequency(double hz, char *out, size_t capacity) { plot::Format::quantity(hz, "Hz", out, capacity, 3); }
void perDivision(unsigned range, char *out, size_t capacity) { // Exact: the table's values are whole or one decimal.
    const uint32_t mv = scope::VoltageRanges::millivolts(range);
    if (mv < 1000) std::snprintf(out, capacity, "%u mV", unsigned(mv));
    else if (mv % 1000) std::snprintf(out, capacity, "%u.%u V", unsigned(mv / 1000), unsigned(mv % 1000 / 100));
    else std::snprintf(out, capacity, "%u V", unsigned(mv / 1000));
}
} // namespace

const char *BodeView::reason(uint16_t f) {
    return f & Point::ClipCh1 ? "CH1 clipped" : f & Point::ClipCh2 ? "CH2 clipped" : f & Point::WeakCh1 ? "no CH1 reference"
        : f & Point::Alias ? "above Nyquist" : f & Point::FewCycles ? "too few cycles" : f & Point::Uncalibrated ? "uncalibrated"
        : f & Point::NoFrame ? "no capture" : f & Point::NoTone ? "no tone" : "incomplete record";
}
const char *BodeView::note(uint16_t f) {
    return f & Point::ClipCh1 ? "CH1 clip" : f & Point::ClipCh2 ? "CH2 clip" : f & Point::WeakCh1 ? "no ref"
        : f & Point::Alias ? "alias" : f & Point::FewCycles ? "few cyc" : f & Point::Uncalibrated ? "no cal"
        : f & Point::NoFrame ? "no rec" : f & Point::NoTone ? "no tone" : "short rec";
}
const char *BodeView::driveName(BodeDrive drive) {
    static constexpr const char *Names[] = {"From GEN", "Dedicated", "Low", "High", "Custom"};
    return unsigned(drive) < 5 ? Names[unsigned(drive)] : "";
}
int BodeView::x(double hz, double startHz, double stopHz) {
    if (!(hz > 0) || !(stopHz > startHz) || !(startHz > 0)) return Left;
    const double t = std::log10(hz / startHz) / std::log10(stopHz / startHz);
    return Left + int(std::lround(std::clamp(t, 0.0, 1.0) * (Right - Left)));
}
void BodeView::create(lv_obj_t *parent, const Theme &theme) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_pos(root_, 0, Top);
    lv_obj_set_size(root_, Width, Height);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(root_, false);
    lv_obj_add_event_cb(root_, draw, LV_EVENT_DRAW_MAIN, this);
    colors(theme);
    lv_obj_set_hidden(root_, true);
}
void BodeView::colors(const Theme &theme) {
    background_ = theme.background; foreground_ = theme.foreground; grid_ = theme.grid; muted_ = theme.muted;
    gain_ = theme.channel2; phase_ = theme.channel1; accent_ = theme.accent;
    if (root_) { lv_obj_set_style_bg_color(root_, lv_color_hex(background_), 0); lv_obj_invalidate(root_); }
}
void BodeView::show(const BodeViewData &data) {
    if (!root_) return;
    data_ = data;
    lv_obj_set_hidden(root_, false);
    lv_obj_move_foreground(root_);
    lv_obj_invalidate(root_);
}
void BodeView::draw(lv_event_t *event) {
    const auto &self = *static_cast<BodeView *>(lv_event_get_user_data(event));
    auto *layer = lv_event_get_layer(event);
    // One part after another (none inside another): the deepest stack is one part's frame and LVGL's.
    self.header(layer);
    if (self.data_.results) self.results(layer);
    else if (self.data_.cursor.table) {
        self.table(layer);
        self.footer(layer, Top + FooterA);
        self.delta(layer, Margin, Top + FooterB);
    } else {
        self.readout(layer, Top + RowB);
        self.delta(layer, Left, Top + RowC); // From the axis: the gain's top label is centred on its line, beside it.
        self.plot(layer);
    }
}
// What is measured (its units in the traces' colours), the state on the right, the progress under it.
void BodeView::header(lv_layer_t *layer) const {
    const auto &d = data_;
    const int y = Top + RowA;
    int x = Margin;
    text(layer, "CH2/CH1", x, y, 60, muted_); x += width("CH2/CH1") + 8;
    text(layer, "gain dB", x, y, 60, gain_); x += width("gain dB") + 8;
    char phase[16]; std::snprintf(phase, sizeof phase, "phase %s", Degree);
    text(layer, phase, x, y, 60, phase_);
    text(layer, d.status, Width - Margin - 130, y, 130, accent_, LV_TEXT_ALIGN_RIGHT);
    if (d.running && d.count) {
        const int end = Margin + int((Width - 2 * Margin) * uint64_t(d.measured) / d.count);
        line(layer, Margin, Top + Progress, Width - Margin, Top + Progress, grid_);
        if (end > Margin) line(layer, Margin, Top + Progress, end, Top + Progress, accent_);
    }
}
// The active cursor's point: its letter, values (a weak CH2: no phase), or why it has none; on the right
// the marker Up/Down went to, else fewer averages than asked, else the stimulus (only where it fits).
void BodeView::readout(lv_layer_t *layer, int y) const {
    const auto &d = data_;
    const char *letter = d.second ? "B" : "A";
    text(layer, letter, Margin, y, 10, accent_);
    const int x = Margin + 12, room = Width - Margin - x;
    if (!d.points || d.cursor.point >= d.measured) {
        text(layer, d.running ? "Measuring the first point" : "No points: F1 Run", x, y, room, muted_);
        return;
    }
    const auto &p = d.points[d.cursor.point];
    char f[16], g[12], ph[12], s[72];
    frequency(p.frequencyHz, f, sizeof f);
    if (!p.valid()) std::snprintf(s, sizeof s, "#%u  %s  invalid: %s", d.cursor.point + 1, f, reason(p.flags));
    else {
        Decimal::fixed(p.gainDb, 2, g, sizeof g);
        if (p.phased()) {
            Decimal::fixed(p.phaseDeg, 1, ph, sizeof ph);
            std::snprintf(s, sizeof s, "#%u  %s  %s dB  %s%s", d.cursor.point + 1, f, g, ph, Degree);
        } else std::snprintf(s, sizeof s, "#%u  %s  %s dB  phase %s  low CH2", d.cursor.point + 1, f, g, NoValue);
    }
    text(layer, s, x, y, room, foreground_);
    static constexpr const char *Names[] = {"", "Peak", "-3 dB", "0\xc2\xb0", "\xc2\xb1" "180\xc2\xb0"};
    char right[24] = "";
    uint32_t color = phase_;
    if (d.cursor.marker != Markers::None) { std::snprintf(right, sizeof right, "%s", Names[d.cursor.marker % 5]); color = accent_; }
    else if (p.valid() && (p.flags & Point::Partial)) { std::snprintf(right, sizeof right, "fewer averages"); color = muted_; }
    else if (p.valid() && p.ch1Vpp > 0) {
        char v[16]; plot::Format::quantity(p.ch1Vpp, "Vpp", v, sizeof v, 3);
        std::snprintf(right, sizeof right, "CH1 %s", v);
    }
    if (right[0] && width(s) + 12 + width(right) <= room) text(layer, right, x, y, room, color, LV_TEXT_ALIGN_RIGHT);
}
// The cursors' difference B - A: Hz, dB and degrees, "--" for a quantity either point lacks (an invalid
// point has a frequency only, a weak CH2 no phase); on the right MENU, the key that swaps them, with the
// active letter in the accent.
void BodeView::delta(lv_layer_t *layer, int left, int y) const {
    const auto &d = data_;
    const auto delta = scope::BodeDelta::of(d.points, d.measured, pointA(), pointB());
    char hz[18] = "--", db[16] = "--", deg[16] = "--", s[72];
    if (delta.frequency) {
        hz[0] = delta.hz < 0 ? '-' : '+';
        plot::Format::quantity(std::fabs(delta.hz), "Hz", hz + (delta.hz != 0), sizeof hz - 1, 3);
    }
    if (delta.gain) signedFixed(delta.db, 2, db, sizeof db);
    if (delta.phase) signedFixed(delta.deg, 1, deg, sizeof deg);
    std::snprintf(s, sizeof s, "\xce\x94 B-A  %s  %s dB  %s%s", hz, db, deg, Degree);
    text(layer, s, left, y, Width - Margin - left, delta.frequency ? foreground_ : muted_);
    // MENU A|B: the active letter in the accent.
    const int keyWidth = width("MENU"), letter = width("A"), bar = width("|");
    const int total = keyWidth + 4 + letter + bar + letter;
    if (width(s) + 10 + total > Width - Margin - left) return;
    int x = Width - Margin - total;
    text(layer, "MENU", x, y, keyWidth + 2, muted_); x += keyWidth + 4;
    text(layer, "A", x, y, letter + 2, d.second ? muted_ : accent_); x += letter;
    text(layer, "|", x, y, bar + 2, muted_); x += bar;
    text(layer, "B", x, y, letter + 2, d.second ? accent_ : muted_);
}
void BodeView::plot(lv_layer_t *layer) const {
    const auto &d = data_;
    const int y0 = Top;
    char a[24];
    // Fit the gain to the measured span. Keep small passband changes visible on the same 58 px panel.
    double low = 0, high = 0; bool any = false;
    for (unsigned i = 0; i < d.measured && d.points; ++i)
        if (d.points[i].valid()) {
            const double g = d.points[i].gainDb;
            low = any ? std::min(low, g) : g; high = any ? std::max(high, g) : g; any = true;
        }
    const double span = high - low;
    int step = 10;
    if (any) {
        for (int spacing : {1, 2, 5, 10, 20, 50}) {
            step = spacing;
            if (span <= 3 * spacing) break;
        }
    }
    const double top = any ? std::ceil(high / step) * step : 10;
    const double bottom = any ? std::min(std::floor(low / step) * step, top - 2 * step) : -30;
    const auto gy = [&](double g) { return y0 + GainTop + int(std::lround((top - g) / (top - bottom) * (GainBottom - GainTop))); };
    const auto py = [&](double p) { return y0 + PhaseTop + int(std::lround((180 - p) / 360 * (PhaseBottom - PhaseTop))); };
    const auto fx = [&](double hz) { return x(hz, d.startHz, d.stopHz); };
    // Frames; the gain's steps (0 dB stronger) and the phase's quarters (0 stronger).
    line(layer, Left, y0 + GainTop, Right, y0 + GainTop, top == 0 ? muted_ : grid_);
    line(layer, Left, y0 + GainBottom, Right, y0 + GainBottom, bottom == 0 ? muted_ : grid_);
    for (int yy : {PhaseTop, PhaseBottom}) line(layer, Left, y0 + yy, Right, y0 + yy, grid_);
    const double first = std::floor((top - 1) / step) * step; // The first step under the top.
    for (double g = first; g > bottom; g -= step) line(layer, Left, gy(g), Right, gy(g), g == 0 ? muted_ : grid_);
    for (int p : {90, 0, -90}) line(layer, Left, py(p), Right, py(p), p ? grid_ : muted_);
    // The gain's labels centred on their lines: 0 dB first, then the ends, then the steps, none over another
    // (the markers' row starts at the axis: the top label beside it).
    const int labelWidth = Left - 3;
    int taken[8]; unsigned labels = 0; // Their centres.
    const auto label = [&](double g) {
        if (g > top || g < bottom || labels == 8) return;
        const int yy = gy(g);
        for (unsigned i = 0; i < labels; ++i) if (std::abs(taken[i] - yy) < Line) return;
        taken[labels++] = yy;
        std::snprintf(a, sizeof a, "%d", int(g)); // Whole steps: integers (newlib-nano prints no %f).
        text(layer, a, 0, yy - 7, labelWidth, gain_, LV_TEXT_ALIGN_RIGHT);
    };
    label(0); label(top); label(bottom);
    for (double g = first; g > bottom; g -= step) label(g);
    text(layer, "180", 0, y0 + PhaseTop + 1, labelWidth, phase_, LV_TEXT_ALIGN_RIGHT);
    text(layer, "0", 0, py(0) - 7, labelWidth, phase_, LV_TEXT_ALIGN_RIGHT);
    text(layer, "-180", 0, y0 + PhaseBottom - 13, labelWidth, phase_, LV_TEXT_ALIGN_RIGHT);
    // Decades, their labels under the phase; the sweep's ends where no decade label is near.
    const int labelY = y0 + PhaseBottom + 1;
    bool startNear = false, stopNear = false;
    for (double decade = std::pow(10, std::floor(std::log10(d.startHz))); decade <= d.stopHz * 1.0001; decade *= 10) {
        if (decade < d.startHz * .9999) continue;
        const int xx = fx(decade);
        line(layer, xx, y0 + GainTop, xx, y0 + GainBottom, grid_);
        line(layer, xx, y0 + PhaseTop, xx, y0 + PhaseBottom, grid_);
        frequency(decade, a, sizeof a);
        const int left = std::clamp(xx - 24, 0, Width - 48); // Whole on the screen at either end.
        text(layer, a, left, labelY, 48, muted_, xx - 24 == left ? LV_TEXT_ALIGN_CENTER : xx < Width / 2 ? LV_TEXT_ALIGN_LEFT : LV_TEXT_ALIGN_RIGHT);
        startNear = startNear || xx - Left < 52;
        stopNear = stopNear || Right - xx < 52;
    }
    if (!startNear) { frequency(d.startHz, a, sizeof a); text(layer, a, 0, labelY, 2 * Left, foreground_, LV_TEXT_ALIGN_CENTER); }
    if (!stopNear) { frequency(d.stopHz, a, sizeof a); text(layer, a, Width - 54, labelY, 54, foreground_, LV_TEXT_ALIGN_RIGHT); }
    // The traces: neighbouring valid points joined; a phase wrap (more than 180 apart) is not joined. A
    // weak CH2 keeps its gain but has no phase: a gap in the phase trace.
    unsigned previous = d.count, before = d.count; // The last valid point; the last one with a phase.
    for (unsigned i = 0; i < d.measured && d.points; ++i) {
        const auto &p = d.points[i];
        if (!p.valid()) { previous = before = d.count; continue; }
        const int xx = fx(p.frequencyHz);
        if (previous < d.count) {
            const auto &q = d.points[previous];
            line(layer, fx(q.frequencyHz), gy(q.gainDb), xx, gy(p.gainDb), gain_, 2);
        } else box(layer, xx - 1, gy(p.gainDb) - 1, xx + 1, gy(p.gainDb) + 1, gain_);
        if (p.phased() && before < d.count && std::fabs(p.phaseDeg - d.points[before].phaseDeg) < 180) {
            const auto &q = d.points[before];
            line(layer, fx(q.frequencyHz), py(q.phaseDeg), xx, py(p.phaseDeg), phase_, 2);
        } else if (p.phased()) box(layer, xx - 1, py(p.phaseDeg) - 1, xx + 1, py(p.phaseDeg) + 1, phase_);
        previous = i;
        before = p.phased() ? i : d.count;
    }
    // The cursors (the other dashed, the active solid over it, each lettered), then the markers on their
    // traces (the one Up/Down went to ringed).
    const auto cursorLine = [&](unsigned point, bool active) {
        if (!d.points || point >= d.measured) return;
        const int xx = fx(d.points[point].frequencyHz);
        if (active) line(layer, xx, y0 + GainTop, xx, y0 + PhaseBottom, foreground_);
        else dashed(layer, xx, y0 + GainTop, y0 + PhaseBottom, muted_);
        const bool b = active == d.second;
        text(layer, b ? "B" : "A", xx + 10 > Right ? xx - 9 : xx + 2, y0 + GainTop + 1, 9, active ? accent_ : muted_);
    };
    cursorLine(d.other, false);
    cursorLine(d.cursor.point, true);
    for (unsigned k = Markers::Peak; k <= Markers::Phase180; ++k) {
        const unsigned i = d.markers.at(Markers::Name(k));
        const bool phase = k >= Markers::Phase0;
        if (i >= d.measured || !d.points || (phase ? !d.points[i].phased() : !d.points[i].valid())) continue;
        const auto &p = d.points[i];
        const int xx = fx(p.frequencyHz), yy = phase ? py(p.phaseDeg) : gy(p.gainDb);
        if (k == d.cursor.marker) box(layer, xx - 4, yy - 4, xx + 4, yy + 4, foreground_);
        box(layer, xx - 2, yy - 2, xx + 2, yy + 2, accent_);
    }
}
void BodeView::table(lv_layer_t *layer) const {
    const auto &d = data_;
    const int y0 = Top;
    // Columns: index, frequency, gain, phase (right-aligned numbers), a note.
    struct Column { int x, width; lv_text_align_t align; };
    static constexpr Column Letter{1, 9, LV_TEXT_ALIGN_LEFT}, Index{10, 20, LV_TEXT_ALIGN_RIGHT}, Frequency{32, 76, LV_TEXT_ALIGN_RIGHT},
        Gain{112, 62, LV_TEXT_ALIGN_RIGHT}, Phase{178, 58, LV_TEXT_ALIGN_RIGHT}, Note{246, 70, LV_TEXT_ALIGN_LEFT};
    const auto cell = [&](const Column &c, const char *s, int y, uint32_t color) { text(layer, s, c.x, y, c.width, color, c.align); };
    char a[24], b[32];
    const int head = y0 + RowB;
    cell(Index, "#", head, muted_); cell(Frequency, "Frequency", head, muted_); cell(Gain, "Gain dB", head, muted_);
    std::snprintf(a, sizeof a, "Phase %s", Degree); cell(Phase, a, head, muted_);
    const unsigned pages = (d.measured + PageRows - 1) / PageRows, first = d.cursor.firstRow();
    if (pages > 1) {
        std::snprintf(a, sizeof a, "%u/%u", first / PageRows + 1, pages);
        text(layer, a, Note.x, head, Note.width, muted_, LV_TEXT_ALIGN_RIGHT);
    }
    if (!d.points || !d.measured)
        text(layer, d.running ? "Measuring the first point" : "No points: F1 Run", Margin, y0 + TableTop, Width - 2 * Margin, muted_);
    for (unsigned r = 0; r < PageRows && d.points && first + r < d.measured; ++r) {
        const unsigned i = first + r;
        const auto &p = d.points[i];
        const int y = y0 + TableTop + int(r) * Line;
        const bool selected = i == d.cursor.point, other = i == d.other && !selected;
        if (selected) box(layer, 0, y, Width - 1, y + Line - 1, accent_);
        else if (other) box(layer, 0, y, Width - 1, y + Line - 1, grid_);
        const uint32_t ink = selected ? background_ : p.valid() ? foreground_ : muted_;
        if (selected || other) cell(Letter, selected == d.second ? "B" : "A", y, selected ? ink : accent_);
        std::snprintf(a, sizeof a, "%u", i + 1); cell(Index, a, y, ink);
        frequency(p.frequencyHz, a, sizeof a); cell(Frequency, a, y, ink);
        if (p.valid()) { Decimal::fixed(p.gainDb, 2, a, sizeof a); cell(Gain, a, y, selected ? ink : gain_); }
        if (p.phased()) { Decimal::fixed(p.phaseDeg, 1, a, sizeof a); cell(Phase, a, y, selected ? ink : phase_); }
        else if (p.valid()) cell(Phase, NoValue, y, ink);
        // The note: why not, a weak CH2, fewer averages than asked, else the markers on it.
        const char *noted = !p.valid() ? note(p.flags) : !p.phased() ? "low CH2" : (p.flags & Point::Partial) ? "part avg" : nullptr;
        if (!noted) {
            static constexpr const char *Tags[] = {"", "Peak", "-3dB", "0\xc2\xb0", "180\xc2\xb0"};
            b[0] = 0;
            for (unsigned k = Markers::Peak; k <= Markers::Phase180; ++k)
                if (d.markers.at(Markers::Name(k)) == i) {
                    const size_t used = std::strlen(b);
                    std::snprintf(b + used, sizeof b - used, "%s%s", used ? " " : "", Tags[k]);
                }
            noted = b;
        }
        cell(Note, noted, y, selected ? ink : p.valid() ? accent_ : muted_);
    }
    line(layer, 0, y0 + FooterA - 2, Width - 1, y0 + FooterA - 2, grid_); // Over the footer (draw() adds it).
}
// Results: label, value, where (muted); "--" for what the run does not qualify; then the run and the rules.
void BodeView::results(lv_layer_t *layer) const {
    const auto &d = data_;
    const auto *r = d.analysis;
    int y = Top + RowB;
    char value[24], where[24], f[16];
    const auto row = [&](const char *label, const char *v, const char *at) {
        text(layer, label, Margin, y, 100, muted_);
        text(layer, v, 108, y, 96, foreground_);
        if (at[0]) text(layer, at, 206, y, Width - Margin - 206, muted_);
        y += Line;
    };
    const auto hertz = [&](const scope::BodeCrossing &c) {
        if (c.found) frequency(c.hz, value, sizeof value); else std::snprintf(value, sizeof value, "%s", NoValue);
        return value;
    };
    const bool any = r && r->peak;
    if (any) { Decimal::fixed(r->peakDb, 2, f, sizeof f); std::snprintf(value, sizeof value, "%s dB", f); frequency(r->peakHz, f, sizeof f); std::snprintf(where, sizeof where, "at %s", f); }
    else { std::snprintf(value, sizeof value, "%s", NoValue); where[0] = 0; }
    row("Peak gain", value, where);
    row("-3 dB low", any ? hertz(r->lower) : NoValue, "");
    row("-3 dB high", any ? hertz(r->upper) : NoValue, "");
    if (any && r->bandwidth) frequency(r->bandwidthHz, value, sizeof value); else std::snprintf(value, sizeof value, "%s", NoValue);
    row("Bandwidth", value, "");
    const auto margin = [&](const char *label, const scope::BodeCrossing *c, const char *unit, unsigned decimals) {
        if (c && c->found) {
            signedFixed(c->value, decimals, f, sizeof f); std::snprintf(value, sizeof value, "%s%s", f, unit);
            frequency(c->hz, f, sizeof f); std::snprintf(where, sizeof where, "at %s", f);
        } else { std::snprintf(value, sizeof value, "%s", NoValue); where[0] = 0; }
        row(label, value, where);
    };
    margin("Gain margin", r ? &r->gainMargin : nullptr, " dB", 2);
    margin("Phase margin", r ? &r->phaseMargin : nullptr, Degree, 1);
    line(layer, 0, y + 1, Width - 1, y + 1, grid_);
    runLines(layer, y + 4);
}
// The run as it ran (its range, density, settle, averages and drive) and the rules of the results.
void BodeView::runLines(lv_layer_t *layer, int y) const {
    const auto &d = data_;
    char s[72], a[16], b[16];
    if (d.run && d.count) {
        frequency(d.startHz, a, sizeof a); frequency(d.stopHz, b, sizeof b);
        std::snprintf(s, sizeof s, "Run %s-%s, %u/dec, settle %u ms, avg %u", a, b, unsigned(d.run->pointsPerDecade),
                      unsigned(d.run->settleMs), unsigned(d.run->averages));
    } else std::snprintf(s, sizeof s, "No run yet: F1 Run");
    text(layer, s, Margin, y, Width - 2 * Margin, muted_);
    if (d.excitation && d.count && !generator::Capabilities::Levels) { // ZT706S: the module's amplitude, no codes.
        char amplitude[16];
        generator::LevelText::name(*d.excitation, amplitude, sizeof amplitude);
        std::snprintf(s, sizeof s, "Drive from GEN: sine, %s (module nominal)", amplitude);
        text(layer, s, Margin, y + Line, Width - 2 * Margin, muted_);
    } else if (d.excitation && d.count) {
        const auto &e = *d.excitation;
        const char *level = generator::Levels::Names[unsigned(e.level) % 4];
        if (e.level == generator::Level::Custom) std::snprintf(s, sizeof s, "Drive %s: sine, DAC codes %u-%u", driveName(d.drive), e.minimumCode, e.maximumCode);
        else if (d.drive == BodeDrive::FromGen) std::snprintf(s, sizeof s, "Drive from GEN: sine, %s level", level);
        else std::snprintf(s, sizeof s, "Drive: sine, %s level", level);
        text(layer, s, Margin, y + Line, Width - 2 * Margin, muted_);
    }
    text(layer, "-3 dB: nearest each side of the peak", Margin, y + 2 * Line, Width - 2 * Margin, muted_);
    text(layer, "Margins: the smallest; -- none qualifies", Margin, y + 3 * Line, Width - 2 * Margin, muted_);
}
// The selected point's stimulus and output (Vpp), or why it has none; its V/div on the right.
void BodeView::footer(lv_layer_t *layer, int y) const {
    const auto &d = data_;
    if (!d.points || d.cursor.point >= d.measured) return;
    const auto &p = d.points[d.cursor.point];
    char s[56], ranges[32], one[12], two[12];
    if (p.valid() && p.ch1Vpp > 0) {
        plot::Format::quantity(p.ch1Vpp, "Vpp", one, sizeof one, 3);
        plot::Format::quantity(p.ch1Vpp * std::pow(10.0, p.gainDb / 20), "Vpp", two, sizeof two, 3);
        std::snprintf(s, sizeof s, "CH1 %s  CH2 %s", one, two);
    } else if (p.valid()) std::snprintf(s, sizeof s, "#%u", d.cursor.point + 1);
    else std::snprintf(s, sizeof s, "#%u invalid: %s", d.cursor.point + 1, reason(p.flags));
    text(layer, s, Margin, y, Width - 2 * Margin, foreground_);
    perDivision(p.range[0], one, sizeof one); perDivision(p.range[1], two, sizeof two);
    std::snprintf(ranges, sizeof ranges, "%s | %s /div", one, two);
    if (width(s) + 12 + width(ranges) <= Width - 2 * Margin) text(layer, ranges, Margin, y, Width - 2 * Margin, muted_, LV_TEXT_ALIGN_RIGHT);
}
} // namespace openosc::ui
