#include "openosc/settings/plot_json.hpp"
#include "openosc/settings/color_value.hpp"

namespace openosc {
void PlotJson::encode(JsonObject root, const plot::Options &o) {
    static constexpr const char *modes[] = {"yt", "xy","fft","yt_fft"}, *styles[] = {"vectors", "points", "envelope"};
    static constexpr const char *grids[] = {"full", "major", "axes", "none"},
                                *math[] = {"off", "add", "subtract"};
    root["mode"] = modes[unsigned(o.mode)];
    root["style"] = styles[unsigned(o.style)];
    root["grid"] = grids[unsigned(o.grid)];
    root["math"] = math[unsigned(o.math)];
    static constexpr const char *reductions[]={"peak","mean","auto"};
    root["reduction"]=reductions[unsigned(o.reduction)];
    constexpr const char *interpolations[]={"linear","sinc","spline"};
    root["interpolation"]=interpolations[unsigned(o.interpolation)];
    root["zoom"] = o.zoom;
    root["zoom_out"] = o.zoomOut; // Zoom & Pan below 1x; older firmware ignores it.
    root["pan_ppb"] = o.panPpb;
    root["pan_permille"] = (o.panPpb + (o.panPpb < 0 ? -500000 : 500000)) / 1000000; // For older firmware.
    root["persistence_ms"] = o.persistenceMs;
    root["averaging"] = o.averaging;
    root["lock_trigger"] = o.lockTrigger;
    root["show_trigger"] = o.showTrigger;
    root["show_zero"] = o.showZero;
    root["glow"] = o.glow;
    root["peak_hold"] = o.peakHold;
    root["show_scale"] = o.showScale;
    auto spectrum=root.createNestedObject("fft");
    constexpr const char *windows[]={"rectangular","hann","hamming","blackman"};
    spectrum["channel"]=o.spectrum.channel;
    spectrum["window"]=windows[unsigned(o.spectrum.window)];
    spectrum["scale"]=o.spectrum.scale==scope::SpectrumScale::Dbfs ? "dbfs" : "linear";
    spectrum["remove_dc"]=o.spectrum.removeDc;
    auto counter=root.createNestedObject("counter");
    counter["source"]=o.counter.source; counter["average"]=o.counter.average; counter["ppm"]=o.counter.ppm;
    auto channels = root.createNestedArray("channels");
    for (const auto &c : o.channels) {
        auto item = channels.createNestedObject();
        item["position_permille"] = c.positionPermille;
        item["gain_percent"] = c.gainPercent;
        item["width"] = c.width;
        item["opacity"] = c.opacity;
        item["visible"] = c.visible;
        item["invert"] = c.invert;
        static constexpr const char *grades[] = {"off", "colors", "mono"};
        item["grade"] = grades[unsigned(c.grade) % 3];
        if (c.gradeColor != plot::ChannelOptions::InheritColor) item["grade_color"] = ColorCatalog::swatch(c.gradeColor).key;
    }
    auto cursor = root.createNestedObject("cursors");
    const auto &c = o.cursors;
    cursor["time"] = c.time;
    cursor["voltage"] = c.voltage;
    cursor["channel"] = c.channel;
    cursor["a_x"] = c.xA;
    cursor["b_x"] = c.xB;
    cursor["a_y"] = c.yA;
    cursor["b_y"] = c.yB;
}
} // namespace openosc
