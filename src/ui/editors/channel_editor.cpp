#include "openosc/ui/editors/scope_editor.hpp"
#include "openosc/acquisition/probe_attenuation.hpp"
#include "openosc/ui/scope/format.hpp"
#include "openosc/settings/color_value.hpp"
#include <algorithm>
#include <cstdio>

namespace openosc::ui {
PropertyKind ChannelEditor::kind(unsigned row) const {
    switch (row) {
    case Enabled: case Invert: return PropertyKind::Toggle;
    case Zero: return PropertyKind::Number;
    default: return PropertyKind::Choice;
    }
}
void ChannelEditor::name(unsigned row, char *output, size_t capacity) const {
    static constexpr const char *names[] = {"Channel", "Enabled", "Coupling", "Probe", "Scale / div", "Analog zero", "Invert",
                                            "Trace brightness", "Density", "Density color"};
    std::snprintf(output, capacity, "%s", row < Fields ? names[row] : "");
}
unsigned ChannelEditor::options(unsigned row) const {
    switch (row) {
    case Channel: case Coupling: case Enabled: case Invert: return 2;
    case Probe: return scope::ProbeAttenuation::Count;
    case Scale: return scope::VoltageRanges::Count;
    case TraceBrightness: return plot::Brightness::choices(plot::Brightness::LowestTrace);
    case Density: return 3;
    case DensityColor: return 1 + ColorCatalog::Swatches; // The channel's own, then the named colours.
    default: return 0;
    }
}
unsigned ChannelEditor::option(unsigned row) const {
    const auto &f = request_.frontend;
    switch (row) {
    case Channel: return channel_;
    case Enabled: return f.enabled[channel_];
    case Coupling: return unsigned(f.coupling[channel_]);
    case Probe: // A ratio outside the list (an older setting) is none of the options until one is chosen.
        for (unsigned i = 0; i < scope::ProbeAttenuation::Count; ++i)
            if (request_.probeDenominator[channel_] == 1 && request_.probeNumerator[channel_] == scope::ProbeAttenuation::Values[i]) return i;
        return scope::ProbeAttenuation::Count;
    case Scale: return f.range[channel_];
    case Invert: return plot_.channels[channel_].invert;
    case TraceBrightness: return plot::Brightness::choice(plot_.channels[channel_].brightness, plot::Brightness::LowestTrace);
    case Density: return unsigned(plot_.channels[channel_].grade);
    case DensityColor: {
        const uint8_t color = plot_.channels[channel_].gradeColor;
        return color == plot::ChannelOptions::InheritColor ? 0 : 1U + color;
    }
    default: return 0;
    }
}
void ChannelEditor::optionName(unsigned row, unsigned index, char *output, size_t capacity) const {
    switch (row) {
    case Channel: std::snprintf(output, capacity, "CH%u", index + 1); break;
    case Coupling: std::snprintf(output, capacity, "%s", index ? "AC" : "DC"); break;
    case Probe: std::snprintf(output, capacity, "%lu:1", (unsigned long)scope::ProbeAttenuation::Values[index % scope::ProbeAttenuation::Count]); break;
    case Scale:
        plot::Format::quantity(scope::AnalogFrontend::millivoltsForRange(index) / 1000.0 *
            request_.probeNumerator[channel_] / request_.probeDenominator[channel_], "V", output, capacity);
        break;
    case TraceBrightness: { // A value between the steps (options()) as it is.
        const unsigned percent = index < options(row) ? plot::Brightness::percent(index, plot::Brightness::LowestTrace)
                                                      : plot_.channels[channel_].brightness;
        std::snprintf(output, capacity, "%u%%", percent);
        break;
    }
    case Density: {
        static constexpr const char *grades[] = {"Off", "Colors", "Mono"};
        std::snprintf(output, capacity, "%s", grades[index % 3]);
        break;
    }
    case DensityColor:
        std::snprintf(output, capacity, "%s", index ? ColorCatalog::swatch(index - 1).name : "Channel");
        break;
    default: PropertyList::optionName(row, index, output, capacity); break;
    }
}
bool ChannelEditor::optionSwatch(unsigned row, unsigned option, uint32_t &rgb) const {
    if (row != DensityColor || !option || option > ColorCatalog::Swatches) return false;
    rgb = ColorCatalog::swatch(option - 1).rgb;
    return true;
}
void ChannelEditor::value(unsigned row, char *output, size_t capacity) const {
    const auto &f = request_.frontend;
    switch (row) {
    case Enabled: {
        // Shows where the record memory goes: this channel's points, or what the other one gets.
        char points[16];
        plot::Format::points(request_.depth(), points, sizeof points);
        if (f.enabled[channel_]) std::snprintf(output, capacity, "On, %s pts", points);
        else std::snprintf(output, capacity, "Off, CH%u %s pts", (channel_ ^ 1) + 1, points);
        break;
    }
    case Probe: std::snprintf(output, capacity, "%lu:%lu", (unsigned long)request_.probeNumerator[channel_],
                              (unsigned long)request_.probeDenominator[channel_]); break;
    case Zero: std::snprintf(output, capacity, "%d/160", f.position[channel_]); break;
    default: PropertyList::value(row, output, capacity); break;
    }
}
bool ChannelEditor::choose(unsigned row, unsigned index) {
    notice_ = nullptr;
    if (index >= options(row)) return false;
    if (row == Probe) {
        request_.probeNumerator[channel_] = scope::ProbeAttenuation::Values[index];
        request_.probeDenominator[channel_] = 1;
        return true;
    }
    if (row == Scale) { request_.frontend.range[channel_] = index; return true; }
    if (row == TraceBrightness) { plot_.channels[channel_].brightness = plot::Brightness::percent(index, plot::Brightness::LowestTrace); return true; }
    if (row == Density) { plot_.channels[channel_].grade = plot::Grade(index); return true; }
    if (row == DensityColor) {
        plot_.channels[channel_].gradeColor = index ? uint8_t(index - 1) : plot::ChannelOptions::InheritColor;
        return true;
    }
    return PropertyList::choose(row, index);
}
void ChannelEditor::adjust(unsigned row, int direction) {
    notice_ = nullptr;
    if (!direction) return;
    auto &frontend = request_.frontend;
    const unsigned other = channel_ ^ 1;
    switch (row) {
    case Channel: channel_ ^= 1; break;
    case Enabled:
        // The record memory stays the same total: switching a channel redistributes it.
        if (frontend.enabled[channel_] && plot_.mode == plot::Mode::XY && frontend.enabled[other]) { notice_ = "X-Y needs CH1 and CH2 on"; break; }
        if (!request_.setChannel(channel_, !frontend.enabled[channel_])) notice_ = channel_ ? "CH2 is the last channel on" : "CH1 is the last channel on";
        break;
    case Coupling: frontend.coupling[channel_] = scope::Coupling(unsigned(frontend.coupling[channel_]) ^ 1); break;
    case Zero: frontend.position[channel_] = std::clamp(frontend.position[channel_] + (direction > 0 ? 1 : -1), 0, 160); break;
    case Invert: plot_.channels[channel_].invert = !plot_.channels[channel_].invert; break;
    default: break;
    }
}
} // namespace openosc::ui
