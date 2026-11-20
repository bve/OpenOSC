#include "openosc/ui/components/function_symbols.hpp"
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/fonts.hpp"
#include <algorithm>
#include <cmath>

namespace openosc::ui {
namespace {
using S = FunctionSymbols::Symbol;
constexpr char TemperatureGlyph[] = u8"\ueb38"; // Tabler Icons Webfont, temperature.
const lv_font_t *temperatureFont(int height) { return height >= 20 ? &openosc_icons_24 : &openosc_icons_12; }
int temperatureWidth(const lv_font_t *font) { return lv_font_get_glyph_width(font, U'\ueb38', 0); }
struct Quantity { const char *letters; bool ac; };
bool quantity(S s, Quantity &q) {
    static constexpr Quantity table[] = {{"V", false}, {"V", true}, {"mV", false}, {"mV", true},
                                         {"A", false}, {"A", true}, {"mA", false}, {"mA", true}};
    if (unsigned(s) > unsigned(S::MilliampsAc)) return false;
    q = table[unsigned(s)];
    return true;
}
unsigned fontSize(int height) { return height >= 20 ? 24 : 12; }
int markWidth(int height) { return std::max(6, int(std::lround(height * .56))); }
// A pen on a symbol box: Tabler coordinates (0..24) scaled to the height, round caps like the SVGs.
struct Pen {
    lv_layer_t *layer;
    float x, y, scale;
    lv_color_t color;
    int stroke() const { return std::max(1, int(std::ceil(2 * scale))); }
    lv_point_precise_t at(float u, float v) const { return {lv_value_precise_t(x + u * scale), lv_value_precise_t(y + v * scale)}; }
    void line(float u1, float v1, float u2, float v2) const {
        lv_draw_line_dsc_t d;
        lv_draw_line_dsc_init(&d);
        d.color = color; d.width = stroke(); d.round_start = d.round_end = 1;
        d.p1 = at(u1, v1); d.p2 = at(u2, v2);
        lv_draw_line(layer, &d);
    }
    void polyline(const float (*points)[2], unsigned count) const {
        for (unsigned i = 1; i < count; ++i) line(points[i - 1][0], points[i - 1][1], points[i][0], points[i][1]);
    }
    void arc(float cu, float cv, float radius, int start, int end) const { // Degrees clockwise from +x.
        lv_draw_arc_dsc_t d;
        lv_draw_arc_dsc_init(&d);
        d.color = color; d.width = stroke(); d.rounded = 1;
        const auto centre = at(cu, cv);
        d.center = {int32_t(std::lround(centre.x)), int32_t(std::lround(centre.y))};
        d.radius = uint16_t(std::lround(radius * scale + stroke() / 2.f));
        d.start_angle = start; d.end_angle = end;
        lv_draw_arc(layer, &d);
    }
};
// The Tabler outlines (docs/ui/icons/tabler/*.svg), path by path.
void omega(const Pen &p) { // M4 19h5v-1 a7.35 7.35 0 1 1 6 0 v1h5
    p.line(4, 19, 9, 19); p.line(9, 19, 9, 18); p.arc(12, 11.29f, 7.35f, 114, 66);
    p.line(15, 18, 15, 19); p.line(15, 19, 20, 19);
}
void sounder(const Pen &p) { // volume-2: the wave, then the speaker (its 1 px corner radii as chamfers)
    p.arc(12, 12, 5, 307, 53);
    static constexpr float body[][2] = {{6, 15}, {4, 15}, {3, 14}, {3, 10}, {4, 9}, {6, 9}, {9.5f, 4.5f}, {11, 5},
                                        {11, 19}, {9.5f, 19.5f}, {6, 15}};
    p.polyline(body, sizeof body / sizeof *body);
}
void diode(const Pen &p) { // circuit-diode
    p.line(22, 12, 16, 12); p.line(2, 12, 8, 12);
    static constexpr float triangle[][2] = {{8, 7}, {16, 12}, {8, 17}, {8, 7}};
    p.polyline(triangle, 4);
    p.line(16, 7, 16, 17);
}
void resistor(const Pen &p) { // Leads and a six-segment zigzag.
    static constexpr float zigzag[][2] = {{1, 12}, {5, 12}, {6.5f, 8}, {9, 16}, {11.5f, 8}, {14, 16}, {16.5f, 8}, {19, 16}, {20, 12}, {23, 12}};
    p.polyline(zigzag, sizeof zigzag / sizeof *zigzag);
}
void capacitor(const Pen &p) { // circuit-capacitor
    p.line(22, 12, 14, 12); p.line(2, 12, 10, 12); p.line(10, 7, 10, 17); p.line(14, 7, 14, 17);
}
} // namespace
FunctionSymbols::Symbol FunctionSymbols::of(dmm::Function f, bool beeper) {
    using F = dmm::Function;
    switch (f) {
    case F::VoltageDc: return S::VoltsDc;
    case F::VoltageAc: return S::VoltsAc;
    case F::MillivoltsDc: return S::MillivoltsDc;
    case F::MillivoltsAc: return S::MillivoltsAc;
    case F::CurrentDc: return S::AmpsDc;
    case F::CurrentAc: return S::AmpsAc;
    case F::MilliampsDc: return S::MilliampsDc;
    case F::MilliampsAc: return S::MilliampsAc;
    case F::Resistance: return S::Ohm;
    case F::Continuity: return S::Continuity;
    case F::Diode: return beeper ? S::DiodeBeeper : S::Diode;
    case F::Capacitance: return S::Capacitor;
    case F::Temperature: return S::Temperature;
    default: return S::Unknown;
    }
}
int FunctionSymbols::width(Symbol s, int height, bool bare) {
    Quantity q;
    if (bare && quantity(s, q)) return Elements::textWidth(q.ac ? "AC" : "DC", fontSize(height));
    if (quantity(s, q)) return Elements::textWidth(q.letters, fontSize(height)) + height / 8 + markWidth(height);
    if (s == S::DiodeBeeper) return 2 * height;
    if (s == S::Temperature) return temperatureWidth(temperatureFont(height));
    if (s == S::Unknown) return Elements::textWidth("?", fontSize(height));
    return height;
}
void FunctionSymbols::draw(lv_layer_t *layer, Symbol s, int x, int y, int height, lv_color_t color, bool bare) {
    const Pen pen{layer, float(x), float(y), height / 24.f, color};
    Quantity q;
    const auto text = [&](const char *value) {
        const bool temperature = s == S::Temperature;
        lv_draw_label_dsc_t d;
        lv_draw_label_dsc_init(&d);
        d.font = temperature ? temperatureFont(height) : Elements::font(fontSize(height));
        d.color = color; d.text = value; d.text_local = 1;
        const int line = lv_font_get_line_height(d.font);
        const int width = temperature ? temperatureWidth(d.font) : Elements::textWidth(value, fontSize(height));
        const int top = y + (height - line) / 2;
        const lv_area_t area = {x, top, x + width, top + line - 1};
        lv_draw_label(layer, &d, &area);
    };
    if (bare && quantity(s, q)) { text(q.ac ? "AC" : "DC"); return; }
    if (bare && s == S::Ohm) { resistor(pen); return; }
    if (quantity(s, q)) {
        text(q.letters);
        // The mark right of the letters, in pixels of this height.
        const int left = x + Elements::textWidth(q.letters, fontSize(height)) + height / 8, w = markWidth(height);
        const auto px = [&](float u, float v, float u2, float v2) {
            lv_draw_line_dsc_t d;
            lv_draw_line_dsc_init(&d);
            d.color = color; d.width = std::max(1, height / 12);
            d.p1 = {lv_value_precise_t(u), lv_value_precise_t(v)}; d.p2 = {lv_value_precise_t(u2), lv_value_precise_t(v2)};
            lv_draw_line(layer, &d);
        };
        if (q.ac) { // One sine period.
            const float mid = y + height * .5f, amplitude = height * .16f;
            float pu = left, pv = mid;
            for (int i = 1; i <= 12; ++i) {
                const float u = left + w * i / 12.f, v = mid - amplitude * std::sin(i * 3.14159265f / 6);
                px(pu, pv, u, v);
                pu = u; pv = v;
            }
        } else { // A line over three dashes.
            const float top = y + std::lround(height * .36f), bottom = y + std::lround(height * .6f);
            px(left, top, left + w, top);
            const float dash = w / 4.f;
            for (int i = 0; i < 3; ++i) px(left + i * dash * 1.5f, bottom, left + i * dash * 1.5f + dash, bottom);
        }
        return;
    }
    switch (s) {
    case S::Ohm: omega(pen); break;
    case S::Continuity: sounder(pen); break;
    case S::Diode: diode(pen); break;
    case S::DiodeBeeper: diode(pen); sounder({layer, float(x + height), float(y), height / 24.f, color}); break;
    case S::Capacitor: capacitor(pen); break;
    case S::Temperature: text(TemperatureGlyph); break;
    default: text("?"); break;
    }
}
} // namespace openosc::ui
