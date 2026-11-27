#include "openosc/settings/plot_json.hpp"
#include "openosc/settings/color_value.hpp"
#include <cstring>
#include <limits>

namespace openosc {
namespace {
template <class T> bool integer(JsonObjectConst object, const char *key, T &output) {
    if (!object.containsKey(key))
        return true;
    if (!object[key].is<int32_t>())
        return false;
    const int64_t value = object[key].as<int32_t>();
    if (value < std::numeric_limits<T>::min() || value > std::numeric_limits<T>::max())
        return false;
    output = T(value);
    return true;
}
// "pan_ppb", else an older "pan_permille" (Options::valid bounds both).
bool pan(JsonObjectConst object, int32_t &output) {
    if (object.containsKey("pan_ppb")) return integer(object, "pan_ppb", output);
    int16_t permille = 0;
    if (!object.containsKey("pan_permille")) return true;
    if (!integer(object, "pan_permille", permille) || permille < -1000 || permille > 1000) return false;
    output = int32_t(permille) * 1000000;
    return true;
}
bool flag(JsonObjectConst object, const char *key, bool &output) {
    if (!object.containsKey(key))
        return true;
    if (!object[key].is<bool>())
        return false;
    output = object[key];
    return true;
}
template <class T, size_t N>
bool choice(JsonObjectConst object, const char *key, T &output, const char *const (&names)[N]) {
    if (!object.containsKey(key))
        return true;
    if (!object[key].is<const char *>())
        return false;
    const char *value = object[key];
    for (unsigned i = 0; i < N; ++i)
        if (!std::strcmp(value, names[i])) {
            output = T(i);
            return true;
        }
    return false;
}
// Themes before the trigger followed its record kept "trigger_position" (0..1000) in the view; the
// record now places its trigger (Viewport), so the key is still checked but never applied.
bool legacyTriggerPosition(JsonObjectConst object) {
    uint16_t position = 0;
    return integer(object, "trigger_position", position) && position <= 1000;
}
} // namespace
bool PlotJson::parse(JsonObjectConst object, plot::Options &output) {
    auto o = output;
    o.spectrum.fullRate = false; // An older plot without fft.full_rate uses whole-record decimation.
    static constexpr const char *modes[] = {"yt", "xy","fft","yt_fft"}, *styles[] = {"vectors", "points", "envelope"};
    static constexpr const char *grids[] = {"full", "major", "axes", "none"},
                                *math[] = {"off", "add", "subtract"};
    static constexpr const char *reductions[]={"peak","mean","auto"}, *interpolations[]={"linear","sinc","spline"};
    if (!choice(object, "mode", o.mode, modes) || !choice(object, "style", o.style, styles) ||
        !choice(object, "grid", o.grid, grids) || !choice(object, "math", o.math, math) ||
        !choice(object,"reduction",o.reduction,reductions) || !choice(object,"interpolation",o.interpolation,interpolations) ||
        !integer(object, "zoom", o.zoom) || !integer(object, "zoom_out", o.zoomOut) || !pan(object, o.panPpb) ||
        !legacyTriggerPosition(object) ||
        !integer(object, "persistence_ms", o.persistenceMs) || !integer(object, "averaging", o.averaging) ||
        !flag(object, "lock_trigger", o.lockTrigger) || !flag(object, "show_trigger", o.showTrigger) ||
        !flag(object, "show_zero", o.showZero) || !flag(object, "glow", o.glow) ||
        !flag(object, "peak_hold", o.peakHold) || !flag(object, "show_scale", o.showScale) ||
        !integer(object, "grid_brightness", o.gridBrightness)) // GitHub #86: absent in older themes (100).
        return false;
    if (object.containsKey("fft")) {
        if (!object["fft"].is<JsonObjectConst>()) return false;
        const auto input=object["fft"].as<JsonObjectConst>();
        constexpr const char *windows[]={"rectangular","hann","hamming","blackman"}, *scales[]={"linear","dbfs"};
        bool removeDc=o.spectrum.removeDc, fullRate=false;
        if (!integer(input,"channel",o.spectrum.channel) || !choice(input,"window",o.spectrum.window,windows) ||
            !choice(input,"scale",o.spectrum.scale,scales) || !flag(input,"remove_dc",removeDc) ||
            !flag(input,"full_rate",fullRate)) return false;
        o.spectrum.removeDc=removeDc;
        o.spectrum.fullRate=fullRate;
    }
    if (object.containsKey("counter")) {
        if (!object["counter"].is<JsonObjectConst>()) return false;
        const auto input=object["counter"].as<JsonObjectConst>();
        if (!integer(input,"source",o.counter.source) || !integer(input,"average",o.counter.average) || !integer(input,"ppm",o.counter.ppm) ||
            !integer(input,"estimate",o.counter.estimate) || !integer(input,"timed",o.counter.timed)) return false; // #172, #173: absent reads 0.
    }
    if (object.containsKey("channels")) {
        auto channels = object["channels"].as<JsonArrayConst>();
        if (channels.size() != 2)
            return false;
        for (unsigned i = 0; i < 2; ++i) {
            if (!channels[i].is<JsonObjectConst>())
                return false;
            const auto input = channels[i].as<JsonObjectConst>();
            auto &c = o.channels[i];
            if (!integer(input, "position_permille", c.positionPermille) ||
                !integer(input, "gain_percent", c.gainPercent) || !integer(input, "width", c.width) ||
                !integer(input, "opacity", c.opacity) || !flag(input, "visible", c.visible) ||
                !flag(input, "invert", c.invert) || !integer(input, "trace_brightness", c.brightness))
                return false;
            // GitHub #85: absent in older themes (Off, the channel's colour).
            static constexpr const char *grades[] = {"off", "colors", "mono"};
            if (!choice(input, "grade", c.grade, grades)) return false;
            if (input.containsKey("grade_color")) {
                if (!input["grade_color"].is<const char *>()) return false;
                const uint8_t swatch = ColorCatalog::byKey(input["grade_color"].as<const char *>());
                if (swatch == ColorValue::Inherit) return false;
                c.gradeColor = swatch;
            }
        }
    }
    if (object.containsKey("cursors")) {
        if (!object["cursors"].is<JsonObjectConst>())
            return false;
        const auto input = object["cursors"].as<JsonObjectConst>();
        auto &c = o.cursors;
        if (!flag(input, "time", c.time) || !flag(input, "voltage", c.voltage) ||
            !integer(input, "channel", c.channel) || !integer(input, "a_x", c.xA) ||
            !integer(input, "b_x", c.xB) || !integer(input, "a_y", c.yA) || !integer(input, "b_y", c.yB))
            return false;
    }
    if (!o.valid())
        return false;
    output = o;
    return true;
}
} // namespace openosc
