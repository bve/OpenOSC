#include "openosc/settings/instrument_json.hpp"
#include "openosc/settings/arrangement_json.hpp"
#include "openosc/settings/color_value.hpp"
#include "openosc/settings/scope_appearance_json.hpp"
#include <cstring>

namespace openosc {
namespace {
template<class T> bool number(JsonObjectConst object, const char *name, T &value) {
    if (!object.containsKey(name)) return true;
    if (!object[name].is<T>()) return false;
    value = object[name].as<T>();
    return true;
}
bool boolean(JsonObjectConst object, const char *name, uint32_t &value) {
    if (!object.containsKey(name)) return true;
    if (!object[name].is<bool>()) return false;
    value = object[name].as<bool>();
    return true;
}
template<size_t N> bool choice(JsonObjectConst object, const char *name, uint32_t &value,
                               const char *const (&names)[N]) {
    if (!object.containsKey(name)) return true;
    if (!object[name].is<const char *>()) return false;
    for (unsigned i = 0; i < N; ++i)
        if (!std::strcmp(object[name], names[i])) { value = i; return true; }
    return false;
}
constexpr const char *couplings[] = {"dc", "ac"}, *edges[] = {"rising", "falling"};
constexpr const char *triggerModes[] = {"auto", "normal", "single"};
constexpr const char *factoryRecordFormats[] = {"auto", "long", "compact"}; // FactoryRecordPolicy.
constexpr const char *displays[] = {"primary", "statistics", "trend", "analog"};
constexpr const char *faces[] = {"ibm", "mono", "segment"}, *palettes[] = {"dark", "amber", "paper"};
static_assert(sizeof displays / sizeof *displays == MeterPreferences::Displays && sizeof faces / sizeof *faces == MeterPreferences::Faces &&
              sizeof palettes / sizeof *palettes == MeterPreferences::Palettes);
// A colour's file text: a named colour by its static key (not copied), Custom as "#rrggbb" (copied
// into the document), Inherit not written.
void color(JsonObject object, const ColorValue &value) {
    if (value.inherit()) return;
    if (!value.isCustom()) { object["color"] = ColorCatalog::swatch(value.index).key; return; }
    char text[ColorText::Capacity];
    if (ColorText::format(value, text, sizeof text)) object["color"] = static_cast<char *>(text);
}
// meter.appearance: {"<role key>": {"color": "<swatch>|#rrggbb", "font": "<font>", "style": "<style>"}}, only the
// overrides. Each field stands alone: an unknown role, value or type is left out (inherited), never
// the reason to drop the rest of the file (older or newer firmware wrote it).
void appearance(JsonObjectConst object, MeterAppearance &output) {
    for (unsigned i = 0; i < MeterAppearance::Roles; ++i) {
        const auto &info = MeterAppearanceCatalog::role(MeterRole(i));
        const auto role = object[info.key];
        if (!role.is<JsonObjectConst>()) continue;
        auto &style = output.roles[i];
        style.setColor(ColorText::parse(role["color"].as<const char *>())); // A key, "#rrggbb", else Inherit.
        style.font = MeterAppearanceCatalog::find(info.fontKeys, info.fonts, role["font"].as<const char *>());
        style.style = MeterAppearanceCatalog::find(info.styleKeys, info.styles, role["style"].as<const char *>());
    }
}
void appearance(JsonObject meter, const MeterAppearance &values) {
    if (!values.custom()) return; // Files without overrides stay as they were.
    auto object = meter.createNestedObject("appearance");
    for (unsigned i = 0; i < MeterAppearance::Roles; ++i) {
        const auto &style = values.roles[i];
        if (style == MeterRoleStyle{}) continue;
        const auto &info = MeterAppearanceCatalog::role(MeterRole(i));
        auto role = object.createNestedObject(info.key);
        color(role, style.colorValue());
        if (style.font != MeterRoleStyle::Inherit) role["font"] = info.fontKeys[style.font];
        if (style.style != MeterRoleStyle::Inherit) role["style"] = info.styleKeys[style.style];
    }
}
// meter.hold: {"mode": "manual|delayed|stable|auto", "delay_ms", "stable_ms", "band_digits", "rearm_digits",
// "min_readings"}. Each value stands alone against HoldSettings::valid (the editor's and SCPI's check):
// one out of range or of another type keeps its default, never its neighbours or the rest of the file.
// The band and the rearm threshold are one pair (rearm > band): taken together when they fit, else
// each alone where it still fits the other.
void hold(JsonObjectConst object, dmm::HoldSettings &output) {
    dmm::HoldSettings value;
    auto take = [&](const char *name, uint32_t dmm::HoldSettings::*field) {
        if (!object[name].is<uint32_t>()) return;
        auto next = value;
        next.*field = object[name].as<uint32_t>();
        if (next.valid()) value = next;
    };
    dmm::HoldMode mode;
    if (dmm::HoldSettings::fromKey(object["mode"].as<const char *>(), mode)) value.mode = mode;
    take("delay_ms", &dmm::HoldSettings::delayMs);
    take("stable_ms", &dmm::HoldSettings::stableMs);
    take("min_readings", &dmm::HoldSettings::minReadings);
    if (object["band_digits"].is<uint32_t>() && object["rearm_digits"].is<uint32_t>()) {
        auto pair = value;
        pair.bandDigits = object["band_digits"].as<uint32_t>();
        pair.rearmDigits = object["rearm_digits"].as<uint32_t>();
        if (pair.valid()) value = pair;
    }
    take("band_digits", &dmm::HoldSettings::bandDigits);
    take("rearm_digits", &dmm::HoldSettings::rearmDigits);
    output = value;
}
void hold(JsonObject meter, const dmm::HoldSettings &values) {
    auto object = meter.createNestedObject("hold");
    object["mode"] = dmm::HoldSettings::key(values.mode);
    object["delay_ms"] = values.delayMs; object["stable_ms"] = values.stableMs;
    object["band_digits"] = values.bandDigits; object["rearm_digits"] = values.rearmDigits;
    object["min_readings"] = values.minReadings;
}
// scope.decoder: every field optional; an invalid one (type, name, range) keeps the decoder's
// defaults whole, never rejects the file (the decoder is not worth losing the Scope's settings).
// The protocols' own parameters in an object each; LIN's lengths as 64 hex digits (one an ID).
constexpr const char *channels[] = {"ch1", "ch2"}, *parities[] = {"none", "even", "odd", "mark", "space"};
constexpr const char *stops[] = {"1", "1.5", "2"}, *aligns[] = {"gap", "manual"}, *checksums[] = {"classic", "enhanced"};
constexpr const char *canSources[] = {"rxd_ch1", "rxd_ch2", "difference"};
static_assert(sizeof canSources / sizeof *canSources == unsigned(decoding::CanSource::Count), "a key per CAN source");
template<class T> bool small(JsonObjectConst object, const char *name, T &value) { // A uint8_t/uint16_t field.
    uint32_t v = value;
    if (!number(object, name, v) || v > 0xFFFFu) return false;
    value = T(v);
    return v == value;
}
template<class T, size_t N> bool named(JsonObjectConst object, const char *name, T &value, const char *const (&names)[N], unsigned offset = 0) {
    uint32_t v = uint32_t(value) - offset;
    if (!choice(object, name, v, names)) return false;
    value = T(v + offset);
    return true;
}
bool flag(JsonObjectConst object, const char *name, uint8_t &value) {
    uint32_t v = value;
    if (!boolean(object, name, v)) return false;
    value = uint8_t(v);
    return true;
}
bool lengths(JsonObjectConst lin, decoding::LinSettings &out) {
    if (!lin.containsKey("lengths")) return true;
    if (!lin["lengths"].is<const char *>()) return false;
    const char *text = lin["lengths"];
    if (std::strlen(text) != decoding::LinSettings::Ids) return false;
    for (unsigned id = 0; id < decoding::LinSettings::Ids; ++id) {
        const char c = text[id];
        if (c < '0' || c > '8') return false;
        out.length(id, unsigned(c - '0'));
    }
    return true;
}
bool protocolsOf(JsonObjectConst object, decoding::Settings &d) {
    const auto part = [&](const char *name) { return object[name].as<JsonObjectConst>(); };
    for (const char *name : {"uart", "i2c", "spi", "lin", "can"})
        if (object.containsKey(name) && !object[name].is<JsonObjectConst>()) return false;
    auto uart = part("uart"), i2c = part("i2c"), spi = part("spi"), lin = part("lin"), can = part("can");
    return number(uart, "baud", d.uart.baud) && small(uart, "bits", d.uart.dataBits) && named(uart, "parity", d.uart.parity, parities) &&
           named(uart, "stop", d.uart.stopHalfBits, stops, 2) && flag(uart, "msb_first", d.uart.msbFirst) &&
           flag(uart, "inverted", d.uart.inverted) && named(i2c, "sda", d.i2c.sdaChannel, channels) &&
           named(spi, "sclk", d.spi.clockChannel, channels) && small(spi, "mode", d.spi.mode) && small(spi, "bits", d.spi.bits) &&
           flag(spi, "msb_first", d.spi.msbFirst) && named(spi, "align", d.spi.manual, aligns) && number(spi, "gap_ns", d.spi.gapNs) &&
           number(spi, "manual_start", d.spi.manualStart) && number(lin, "baud", d.lin.baud) &&
           named(lin, "checksum", d.lin.enhanced, checksums) && lengths(lin, d.lin) && number(can, "bitrate", d.can.bitrate) &&
           small(can, "sample_point", d.can.samplePointPermille) && named(can, "source", d.can.source, canSources) &&
           number(can, "difference_mv", d.can.differenceMv) && number(can, "data_bitrate", d.can.dataBitrate) &&
           small(can, "data_sample_point", d.can.dataSamplePointPermille);
}
void decoder(JsonObjectConst object, decoding::Settings &output) {
    using decoding::Protocols;
    decoding::Settings d;
    uint32_t protocol = uint32_t(d.protocol), view = uint32_t(d.view), update = uint32_t(d.update);
    // A file from before the Auto hysteresis with its hysteresis_mv: that value stays in force (Manual).
    uint32_t hysteresis = uint32_t(object.containsKey("hysteresis_mv") ? decoding::Hysteresis::Manual : d.hysteresis);
    static constexpr const char *protocols[] = {"off", "uart", "i2c", "spi", "lin", "can"};
    static constexpr const char *views[] = {"annotations", "split", "table"}, *updates[] = {"complete", "live"};
    static constexpr const char *hysteresisModes[] = {"auto", "manual"};
    static_assert(sizeof hysteresisModes / sizeof *hysteresisModes == unsigned(decoding::Hysteresis::Count));
    static_assert(sizeof protocols / sizeof *protocols == unsigned(decoding::Protocol::Count) &&
                  sizeof views / sizeof *views == unsigned(decoding::View::Count) && sizeof updates / sizeof *updates == unsigned(decoding::Update::Count));
    bool ok = boolean(object, "enabled", d.enabled) && choice(object, "protocol", protocol, protocols) && choice(object, "view", view, views) &&
              choice(object, "update", update, updates) && choice(object, "hysteresis", hysteresis, hysteresisModes) &&
              number(object, "channel", d.channel) &&
              number(object, "hysteresis_mv", d.hysteresisMv) && number(object, "min_pulse_ns", d.minPulseNs) && protocolsOf(object, d);
    if (ok && object.containsKey("threshold_mv")) {
        const auto t = object["threshold_mv"].as<JsonArrayConst>();
        ok = t.size() == 2 && t[0].is<int32_t>() && t[1].is<int32_t>();
        if (ok) { d.thresholdMv[0] = t[0]; d.thresholdMv[1] = t[1]; }
    }
    d.protocol = decoding::Protocol(protocol); d.view = decoding::View(view); d.update = decoding::Update(update);
    d.hysteresis = decoding::Hysteresis(hysteresis);
    if (d.update == decoding::Update::Live && !Protocols::liveAvailable()) d.update = decoding::Update::Complete;
    if (!Protocols::available(d.protocol)) d.enabled = 0; // "off": nothing decoded.
    if (ok && d.valid()) output = d;
}
void encodeDecoder(JsonObject scope, const decoding::Settings &d) {
    auto object = scope.createNestedObject("decoder");
    object["enabled"] = bool(d.enabled); object["protocol"] = decoding::Protocols::key(d.protocol);
    object["view"] = decoding::Protocols::viewKey(d.view); object["update"] = decoding::Protocols::updateKey(d.update);
    object["channel"] = d.channel;
    auto t = object.createNestedArray("threshold_mv"); t.add(d.thresholdMv[0]); t.add(d.thresholdMv[1]);
    object["hysteresis"] = decoding::Protocols::hysteresisKey(d.hysteresis);
    object["hysteresis_mv"] = d.hysteresisMv; object["min_pulse_ns"] = d.minPulseNs;
    auto uart = object.createNestedObject("uart");
    uart["baud"] = d.uart.baud; uart["bits"] = d.uart.dataBits; uart["parity"] = parities[d.uart.parity];
    uart["stop"] = stops[d.uart.stopHalfBits - 2]; uart["msb_first"] = bool(d.uart.msbFirst); uart["inverted"] = bool(d.uart.inverted);
    object.createNestedObject("i2c")["sda"] = channels[d.i2c.sdaChannel];
    auto spi = object.createNestedObject("spi");
    spi["sclk"] = channels[d.spi.clockChannel]; spi["mode"] = d.spi.mode; spi["bits"] = d.spi.bits;
    spi["msb_first"] = bool(d.spi.msbFirst); spi["align"] = aligns[d.spi.manual]; spi["gap_ns"] = d.spi.gapNs;
    spi["manual_start"] = d.spi.manualStart;
    auto lin = object.createNestedObject("lin");
    lin["baud"] = d.lin.baud; lin["checksum"] = checksums[d.lin.enhanced];
    char text[decoding::LinSettings::Ids + 1];
    for (unsigned id = 0; id < decoding::LinSettings::Ids; ++id) text[id] = char('0' + d.lin.length(id));
    text[decoding::LinSettings::Ids] = 0;
    lin["lengths"] = text; // Copied by the document (a char array, not a literal).
    auto can = object.createNestedObject("can");
    can["bitrate"] = d.can.bitrate; can["sample_point"] = d.can.samplePointPermille;
    can["source"] = canSources[unsigned(d.can.source)]; can["difference_mv"] = d.can.differenceMv;
    can["data_bitrate"] = d.can.dataBitrate; can["data_sample_point"] = d.can.dataSamplePointPermille;
}
}
bool InstrumentJson::parse(JsonObjectConst scope, JsonObjectConst meter, Preferences &output) {
    auto &s = output.scope;
    // "record_points" is the total of the enabled channels. Files before it stored "samples", a
    // depth per channel (4..65536); it migrates to the smallest budget keeping at least that depth.
    uint32_t legacySamples = 0;
    const bool budget = scope.containsKey("record_points"), legacy = !budget && scope.containsKey("samples");
    if ((budget && !number(scope, "record_points", s.recordPoints)) || (legacy && !number(scope, "samples", legacySamples)) ||
        (legacy && (legacySamples < 4 || legacySamples > 65536 || (legacySamples & 3)))) return false;
    if (!(scope.containsKey("run_mode") ? number(scope,"run_mode",s.runMode) : boolean(scope,"high_performance",s.runMode)) || !boolean(scope,"roll_auto",s.rollAuto) || !boolean(scope,"interleave",s.interleave) ||
        !choice(scope, "factory_record_format", s.factoryRecordFormat, factoryRecordFormats) ||
        !choice(meter, "display", output.meter.display, displays) ||
        !boolean(meter, "statistics", output.meter.statistics) || !boolean(meter, "bar_graph", output.meter.bar) || !boolean(meter,"auto",output.meter.automatic) ||
        !number(meter,"range_index",output.meter.range) || !choice(meter, "face", output.meter.face, faces) ||
        !choice(meter, "palette", output.meter.palette, palettes)) return false;
    if (meter["appearance"].is<JsonObjectConst>()) appearance(meter["appearance"].as<JsonObjectConst>(), output.meter.appearance);
    if (meter["hold"].is<JsonObjectConst>()) hold(meter["hold"].as<JsonObjectConst>(), output.meter.hold);
    arrangement(meter, "arrangement_digital", output.meter.digitalArrangement);
    arrangement(meter, "arrangement_analog", output.meter.analogArrangement);
    arrangement(scope, "arrangement", s.arrangement);
    if (scope["appearance"].is<JsonObjectConst>()) ScopeAppearanceJson::parse(scope["appearance"].as<JsonObjectConst>(), s.appearance);
    if (scope["decoder"].is<JsonObjectConst>()) decoder(scope["decoder"].as<JsonObjectConst>(), s.decoder);
    if (meter.containsKey("range_function")) { // The owner of range_index; absent: unknown.
        if (!meter["range_function"].is<const char *>()) return false;
        unsigned f = 0;
        while (f < MeterFunctionCount && std::strcmp(meter["range_function"], MeterFunctions[f].key)) ++f;
        if (f == MeterFunctionCount) return false;
        output.meter.rangeFunction = f;
    }
    if (scope.containsKey("channels")) {
        const auto channels = scope["channels"].as<JsonArrayConst>();
        if (channels.size() != 2) return false;
        for (unsigned c = 0; c < 2; ++c) {
            if (!channels[c].is<JsonObjectConst>()) return false;
            const auto channel = channels[c].as<JsonObjectConst>();
            if (!boolean(channel, "enabled", s.enabled[c]) || !choice(channel, "coupling", s.coupling[c], couplings) ||
                !number(channel, "position", s.position[c]) || !number(channel, "probe_numerator", s.probeNumerator[c]) ||
                !number(channel, "probe_denominator", s.probeDenominator[c])) return false;
        }
    }
    if (scope.containsKey("trigger")) {
        if (!scope["trigger"].is<JsonObjectConst>()) return false;
        const auto trigger = scope["trigger"].as<JsonObjectConst>();
        if (!number(trigger, "source", s.triggerSource) || !number(trigger, "level_codes", s.triggerLevel) ||
            !number(trigger,"position_permille",s.triggerPosition) || !boolean(trigger,"noise_reject",s.triggerReject) ||
            !choice(trigger, "edge", s.triggerEdge, edges) || !choice(trigger, "mode", s.triggerMode, triggerModes)) return false;
    }
    if (scope.containsKey("measurements")) {
        const auto measurements = scope["measurements"].as<JsonArrayConst>();
        if (measurements.size()!=2) return false;
        if (measurements[0].is<uint32_t>() && measurements[1].is<uint32_t>()) {
            for (auto &channel : s.measurements.values) {
                channel[0]=measurements[0]; channel[1]=measurements[1];
                channel[2]=channel[3]=unsigned(scope::Measurement::Off);
            }
        } else for (unsigned c=0; c<2; ++c) {
            const auto channel=measurements[c].as<JsonArrayConst>();
            if (channel.size()!=scope::ReadoutSelection::PerChannel) return false;
            for (unsigned i=0; i<scope::ReadoutSelection::PerChannel; ++i) {
                if (!channel[i].is<uint32_t>()) return false;
                s.measurements.values[c][i]=channel[i];
            }
        }
    }
    if (scope.containsKey("measure_basis")) {
        const char *basis=scope["measure_basis"] | "";
        if (!std::strcmp(basis,"screen")) s.measurements.basis=scope::ReadoutBasis::Screen;
        else if (!std::strcmp(basis,"record")) s.measurements.basis=scope::ReadoutBasis::Record;
        else return false;
    }
    if (legacy) s.recordPoints = scope::RecordBudget::covering(legacySamples, scope::RecordBudget::channels(s.enabled[0], s.enabled[1]));
    return s.valid() && output.meter.valid();
}
void InstrumentJson::encode(JsonObject scope, JsonObject meter, const Preferences &values) {
    const auto &s = values.scope;
    scope["record_points"] = s.recordPoints;
    scope["run_mode"] = s.runMode;
    scope["roll_auto"]=bool(s.rollAuto);
    scope["interleave"]=bool(s.interleave);
    if (s.factoryRecordFormat) scope["factory_record_format"] = factoryRecordFormats[s.factoryRecordFormat]; // Absent: auto.
    auto channels = scope.createNestedArray("channels");
    for (unsigned c = 0; c < 2; ++c) {
        auto channel = channels.createNestedObject();
        channel["enabled"] = bool(s.enabled[c]); channel["coupling"] = couplings[s.coupling[c]];
        channel["position"] = s.position[c]; channel["probe_numerator"] = s.probeNumerator[c];
        channel["probe_denominator"] = s.probeDenominator[c];
    }
    auto trigger = scope.createNestedObject("trigger");
    trigger["source"] = s.triggerSource; trigger["edge"] = edges[s.triggerEdge];
    trigger["mode"] = triggerModes[s.triggerMode]; trigger["level_codes"] = s.triggerLevel;
    trigger["position_permille"]=s.triggerPosition;
    trigger["noise_reject"]=bool(s.triggerReject);
    auto measurements = scope.createNestedArray("measurements");
    for (const auto &channel : s.measurements.values) {
        auto row=measurements.createNestedArray();
        for (auto value : channel) row.add(value);
    }
    scope["measure_basis"] = s.measurements.basis==scope::ReadoutBasis::Record ? "record" : "screen";
    arrangement(scope, "arrangement", s.arrangement);
    ScopeAppearanceJson::encode(scope, s.appearance);
    encodeDecoder(scope, s.decoder);
    meter["display"] = displays[values.meter.display]; meter["statistics"] = bool(values.meter.statistics); meter["bar_graph"] = bool(values.meter.bar);
    meter["auto"] = bool(values.meter.automatic); meter["range_index"] = values.meter.range;
    meter["face"] = faces[values.meter.face]; meter["palette"] = palettes[values.meter.palette];
    arrangement(meter, "arrangement_digital", values.meter.digitalArrangement);
    arrangement(meter, "arrangement_analog", values.meter.analogArrangement);
    if (values.meter.rangeFunction < MeterFunctionCount) meter["range_function"] = MeterFunctions[values.meter.rangeFunction].key;
    appearance(meter, values.meter.appearance);
    hold(meter, values.meter.hold);
}
} // namespace openosc
