#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/ui/scope/painter.hpp"
#include "openosc/measurement/dmm/reading_trace.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::plot {
void Painter::cursorLine(CursorLine name, const char *label, int x1, int y1, int x2, int y2, int tagX, int tagY, uint32_t color) {
    const bool active = model_.cursorFocus() == name;
    line(x1, y1, x2, y2, color, active ? 255 : 170, !active, active ? 2 : 1);
    if (active) tag(label, tagX, tagY, palette_.tagInk(color), color); // Plain lines carry no box: it marks the active one.
    else text(label, tagX + 2, tagY, color, 12);
}
void Painter::cursors() {
    const auto &c = model_.options().cursors;
    const auto &view = model_.viewport();
    const auto readout = model_.cursors();
    const int w = view.width - 1, h = view.height - 1;
    if (w < 24 || h < 40)
        return;
    char values[2][40], textBuffer[112];
    const bool spectrum = model_.options().mode == Mode::Spectrum;
    // The bottom row belongs to the frequency scale of FFT and to the AVG mark.
    int row = h - (spectrum || model_.options().averaging > 1 ? 28 : 14);
    const auto readoutText = [&](const char *value, uint32_t color) {
        text(value, 4, row, color, ui::Elements::textWidth(value) + 2, true);
        row -= 14;
    };
    if (c.time && (readout.timeValid || readout.frequencyValid)) {
        const int a = cursorPixel(c.xA, w), b = cursorPixel(c.xB, w);
        const int y = readout.frequencyValid ? 32 : 12; // Under the FFT caption.
        cursorLine(CursorLine::TimeA, "A", a, 0, a, h, std::min(a + 2, w - 12), y, palette_.cursor);
        cursorLine(CursorLine::TimeB, "B", b, 0, b, h, std::min(b + 2, w - 12), y, palette_.cursor);
        if (readout.frequencyValid) {
            Format::quantity(readout.deltaFrequencyHz, "Hz", values[0], sizeof values[0]);
            std::snprintf(textBuffer, sizeof textBuffer, "dF %s", values[0]);
        } else {
            Format::quantity(readout.deltaSeconds, "s", values[0], sizeof values[0]);
            if (readout.reciprocalValid)
                Format::quantity(readout.reciprocalHz, "Hz", values[1], sizeof values[1]);
            else
                std::snprintf(values[1], sizeof values[1], "--");
            std::snprintf(textBuffer, sizeof textBuffer, "dT %s  1/dT %s", values[0], values[1]);
        }
        readoutText(textBuffer, palette_.cursor);
    }
    // The DMM trace's level cursors (Y-T, Y-T+FFT): their difference in its divisions times its units/div.
    if (c.voltage && c.channel == CursorOptions::Meter && !spectrum && model_.options().meter.shown) {
        const auto color = palette_.meter;
        const int a = cursorPixel(c.yA, h), b = cursorPixel(c.yB, h);
        cursorLine(CursorLine::LevelA, "a", 0, a, w, a, w - 14, std::min(a + 1, h - 14), color);
        cursorLine(CursorLine::LevelB, "b", 0, b, w, b, w - 14, std::min(b + 1, h - 14), color);
        const auto quantity = meter_ ? meter_->quantity() : Quantity::Unknown;
        const unsigned index = MeterScale::scaled(quantity) ? MeterScale::index(quantity) : 0;
        if (!MeterScale::scaled(quantity) ||
            !Format::quantity(double(a - b) / (double(h) / 8) * MeterScale::perDivision(model_.options().meter.scales[index]),
                              MeterScale::Spans[index].unit, values[0], sizeof values[0]))
            std::snprintf(values[0], sizeof values[0], "--");
        std::snprintf(textBuffer, sizeof textBuffer, "dDMM %s", values[0]);
        readoutText(textBuffer, color);
    } else if (c.voltage && (readout.codesValid || readout.decibelsValid)) {
        const auto color = palette_.channel(spectrum ? model_.options().spectrum.channel : c.channel);
        const int a = cursorPixel(c.yA, h), b = cursorPixel(c.yB, h);
        cursorLine(CursorLine::LevelA, "a", 0, a, w, a, w - 14, std::min(a + 1, h - 14), color);
        cursorLine(CursorLine::LevelB, "b", 0, b, w, b, w - 14, std::min(b + 1, h - 14), color);
        Format::quantity(readout.decibelsValid ? readout.deltaDecibels : readout.voltsValid ? readout.deltaVolts : readout.deltaCodes,
                         readout.decibelsValid ? "dB" : readout.voltsValid ? "V" : "cnt", values[0], sizeof values[0]);
        std::snprintf(textBuffer, sizeof textBuffer, "%s %s", readout.decibelsValid ? "dL" : readout.voltsValid ? "dV" : "dC", values[0]);
        readoutText(textBuffer, color);
    }
}
} // namespace openosc::plot
