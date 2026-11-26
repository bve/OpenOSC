#include "openosc/settings/theme_json.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "openosc/settings/plot_json.hpp"
#include <cstdio>

namespace openosc {
namespace {
void color(JsonObject object, const char* name, uint32_t value) {
    char text[8];
    std::snprintf(text, sizeof text, "#%06lX", static_cast<unsigned long>(value & 0xffffff));
    object[name] = text;
}
template <class T> void overrideValue(JsonObject object, const char *name, T value, T fallback) {
    if (value != fallback) object[name] = value;
}
}
size_t ThemeJson::encode(const Theme& theme, char* output, size_t capacity) {
    if(!theme.plot.valid())return 0;
    JsonWorkspace::Document<ThemeDocumentCapacity> lease;
    if (!lease) return 0;
    auto &document = *lease;
    document["schema"] = 1;
    document["palette_revision"] = 1; // A deliberate MATH/CH2 match stays explicit after saving (#184).
    static constexpr const char *layouts[] = {"cards", "compact", "legacy"};
    if (unsigned(theme.layout) >= 3) return 0;
    document["layout"] = layouts[unsigned(theme.layout)];
    const auto defaults = Theme::defaults(theme.layout);
    PlotJson::encode(document.createNestedObject("plot"),theme.plot);
    auto colors = document.createNestedObject("colors");
    color(colors, "background", theme.background); color(colors, "foreground", theme.foreground);
    color(colors, "accent", theme.accent); color(colors, "grid", theme.grid);
    color(colors, "channel1", theme.channel1); color(colors, "channel2", theme.channel2);
    color(colors, "panel", theme.panel); color(colors, "muted", theme.muted); color(colors, "generator", theme.generator);
    color(colors,"math",theme.math);color(colors,"reference",theme.reference);color(colors,"cursor",theme.cursor);
    for (unsigned mode = 0; mode < InstrumentModeCount; ++mode) {
        auto screen = document.createNestedArray(ThemeScreenKeys[mode]);
        for (unsigned i = 0; i < ElementCount; ++i) {
            const auto& e = theme.elements[mode][i];
            const auto& base = defaults.elements[mode][i];
            auto element = screen.createNestedObject();
            element["id"] = ElementNames[i];
            overrideValue(element, "x", e.x, base.x); overrideValue(element, "y", e.y, base.y);
            overrideValue(element, "width", e.width, base.width);
            overrideValue(element, "height", e.height, base.height);
            overrideValue(element, "visible", e.visible, base.visible);
            overrideValue(element, "font", e.font, base.font);
            overrideValue(element, "z", e.zOrder, base.zOrder);
            color(element, "color", e.color);
            if (e.icon[0]) element["icon"] = e.icon;
        }
    }
    if (document.overflowed() || measureJsonPretty(document) >= capacity) return 0;
    return serializeJsonPretty(document, output, capacity);
}
}
