#include "openosc/settings/instrument_json.hpp"
#include "openosc/settings/arrangement_json.hpp"
#include "openosc/settings/color_value.hpp"
#include "openosc/settings/scope_appearance_json.hpp"
#include "openosc/acquisition/bode.hpp"
#include <cstring>

namespace openosc {
namespace {
template<class T> bool number(JsonObjectConst object, const char *name, T &value) {
    if (!object.containsKey(name)) return true;
    if (!object[name].is<T>()) return false;
    value = object[name].as<T>();
    return true;
}
template<class T> bool boolean(JsonObjectConst object, const char *name, T &value) {
    if (!object.containsKey(name)) return true;
    if (!object[name].is<bool>()) return false;
    value = object[name].as<bool>();
    return true;
}
template<class T, size_t N> bool choice(JsonObjectConst object, const char *name, T &value,
                               const char *const (&names)[N]) {
    if (!object.containsKey(name)) return true;
    if (!object[name].is<const char *>()) return false;
    for (unsigned i = 0; i < N; ++i)
        if (!std::strcmp(object[name], names[i])) { value = T(i); return true; }
    return false;
}
constexpr const char *couplings[] = {"dc", "ac"}, *edges[] = {"rising", "falling"};
constexpr const char *triggerModes[] = {"auto", "normal", "single"};
constexpr const char *triggerTypes[] = {"edge", "width", "runt", "transition"};
constexpr const char *triggerConditions[] = {"any", "less", "greater", "inside", "outside"};
constexpr const char *junctionSources[] = {"manual", "startup", "live"};
constexpr const char *factoryRecordFormats[] = {"auto", "long", "compact", "mkr"}; // FactoryRecordPolicy.
constexpr const char *displays[] = {"primary", "statistics", "trend", "analog"};
constexpr const char *faces[] = {"ibm", "mono", "segment"}, *palettes[] = {"dark", "amber", "paper"};
constexpr const char *presets[] = {"custom", "hybrid"}; // meter.appearance_preset (MeterPreferences::preset).
// scope.keys (ScopeKeys): AUTO and the F2/F4 holds, in the order of their enums.
constexpr const char *autoKeys[] = {"quick", "classic"}, *resetHolds[] = {"home_view", "reset_zoom_pan"};
constexpr const char *shortcuts[] = {"cursor_settings", "measurements", "acquisition", "record_memory", "interleave_ch1"};
static_assert(sizeof autoKeys / sizeof *autoKeys == unsigned(ScopeKeys::Auto::Count) &&
              sizeof resetHolds / sizeof *resetHolds == unsigned(ScopeKeys::Reset::Count) &&
              sizeof shortcuts / sizeof *shortcuts == unsigned(ScopeKeys::Shortcut::Count));
// Each field stands alone: an unknown name or type, or a destination this board does not offer,
// keeps that field's default, never the reason to drop the rest of the file.
template<class E, size_t N> void option(JsonObjectConst object, const char *name, E &output, const char *const (&names)[N]) {
    uint32_t value = uint32_t(output);
    if (choice(object, name, value, names)) output = E(value);
}
void keys(JsonObjectConst object, ScopeKeys &output) {
    option(object, "auto", output.automatic, autoKeys);
    option(object, "f2_hold", output.reset, resetHolds);
    auto shortcut = output.shortcut;
    option(object, "f4_hold", shortcut, shortcuts);
    if (ScopeKeys::available(shortcut)) output.shortcut = shortcut;
}
static_assert(sizeof displays / sizeof *displays == MeterPreferences::Displays && sizeof faces / sizeof *faces == MeterPreferences::Faces &&
              sizeof palettes / sizeof *palettes == MeterPreferences::Palettes && sizeof presets / sizeof *presets == MeterPreferences::Presets);
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
// "min_readings", "zero_digits"}. Each value stands alone against HoldSettings::valid (the editor's and SCPI's check):
// one out of range or of another type keeps its default, never its neighbours or the rest of the file.
// The band and the rearm threshold are one pair (rearm > band): taken together when they fit, else
// each alone where it still fits the other.
void hold(JsonObjectConst object, dmm::HoldSettings &output) {
    dmm::HoldSettings value;
    auto take = [&](const char *name, uint16_t dmm::HoldSettings::*field) {
        if (!object[name].is<uint16_t>()) return;
        auto next = value;
        next.*field = object[name].as<uint16_t>();
        if (next.valid()) value = next;
    };
    dmm::HoldMode mode;
    if (dmm::HoldSettings::fromKey(object["mode"].as<const char *>(), mode)) value.mode = mode;
    take("delay_ms", &dmm::HoldSettings::delayMs);
    take("stable_ms", &dmm::HoldSettings::stableMs);
    take("min_readings", &dmm::HoldSettings::minReadings);
    take("zero_digits", &dmm::HoldSettings::zeroDigits);
    if (object["band_digits"].is<uint16_t>() && object["rearm_digits"].is<uint16_t>()) {
        auto pair = value;
        pair.bandDigits = object["band_digits"].as<uint16_t>();
        pair.rearmDigits = object["rearm_digits"].as<uint16_t>();
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
    object["min_readings"] = values.minReadings; object["zero_digits"] = values.zeroDigits;
}
// meter.log (GitHub #144) as [mode, interval_s, duration_s, threshold_permille], mode 0 interval or 1 event:
// numbers only, the shortest form (the longest file keeps a fifth of the JSON string budget free).
// Each element stands alone against LogSettings' lists (a value outside them, of another type, missing):
// that one keeps its default. Absent (files before it) or not an array: the defaults.
void sessionLog(JsonArrayConst array, dmm::LogSettings &output) {
    dmm::LogSettings value;
    if (array[0].is<uint32_t>() && array[0].as<uint32_t>() < unsigned(dmm::LogMode::Count)) value.mode(dmm::LogMode(array[0].as<uint32_t>()));
    if (array[1].is<uint32_t>()) value.intervalS(array[1].as<uint32_t>());
    if (array[2].is<uint32_t>()) value.durationS(array[2].as<uint32_t>());
    if (array[3].is<uint32_t>()) value.thresholdPermille(array[3].as<uint32_t>());
    output = value;
}
void sessionLog(JsonObject meter, const dmm::LogSettings &values) {
    auto array = meter.createNestedArray("log");
    array.add(unsigned(values.mode())); array.add(values.intervalS()); array.add(values.durationS()); array.add(values.thresholdPermille());
}
// scope.decoder: every field optional; an invalid one (type, name, range) keeps the decoder's
// defaults whole, never rejects the file (the decoder is not worth losing the Scope's settings).
// The protocols' own parameters in an object each; LIN's lengths as 64 hex digits (one an ID).
constexpr const char *channels[] = {"ch1", "ch2"}, *parities[] = {"none", "even", "odd", "mark", "space"};
constexpr const char *stops[] = {"1", "1.5", "2"}, *aligns[] = {"gap", "manual"}, *checksums[] = {"classic", "enhanced"};
constexpr const char *canSources[] = {"rxd_ch1", "rxd_ch2", "difference"};
static_assert(sizeof canSources / sizeof *canSources == unsigned(decoding::CanSource::Count), "a key per CAN source");
constexpr const char *rs485Sources[] = {"diff", "ro"};
static_assert(sizeof rs485Sources / sizeof *rs485Sources == unsigned(decoding::Rs485Source::Count), "a key per RS-485 source");
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
// RS-485 / DMX512 (GitHub #163) as [threshold_mv, "diff" | "ro", inverted]: the shortest form the file's
// string budget leaves room for (settings_layout); absent at the defaults.
bool rs485Of(JsonObjectConst object, decoding::Rs485Settings &out) {
    if (!object.containsKey("rs485")) return true;
    const auto a = object["rs485"].as<JsonArrayConst>();
    if (!object["rs485"].is<JsonArrayConst>() || a.size() != 3 || !a[0].is<int32_t>() || !a[1].is<const char *>() || !a[2].is<bool>())
        return false;
    for (unsigned i = 0; i < unsigned(decoding::Rs485Source::Count); ++i)
        if (!std::strcmp(a[1].as<const char *>(), rs485Sources[i])) {
            out.thresholdMv = a[0].as<int32_t>(); out.source = decoding::Rs485Source(i); out.inverted = a[2].as<bool>();
            return true;
        }
    return false;
}
bool protocolsOf(JsonObjectConst object, decoding::Settings &d) {
    const auto part = [&](const char *name) { return object[name].as<JsonObjectConst>(); };
    for (const char *name : {"uart", "i2c", "spi", "lin", "can"})
        if (object.containsKey(name) && !object[name].is<JsonObjectConst>()) return false;
    auto uart = part("uart"), i2c = part("i2c"), spi = part("spi"), lin = part("lin"), can = part("can");
    return number(uart, "baud", d.uart.baud) && small(uart, "bits", d.uart.dataBits) && named(uart, "parity", d.uart.parity, parities) &&
           named(uart, "stop", d.uart.stopHalfBits, stops, 2) && flag(uart, "msb_first", d.uart.msbFirst) &&
           flag(uart, "inverted", d.uart.inverted) && number(uart, "rs232_threshold_mv", d.uart.rs232ThresholdMv) &&
           named(i2c, "sda", d.i2c.sdaChannel, channels) &&
           named(spi, "sclk", d.spi.clockChannel, channels) && small(spi, "mode", d.spi.mode) && small(spi, "bits", d.spi.bits) &&
           flag(spi, "msb_first", d.spi.msbFirst) && named(spi, "align", d.spi.manual, aligns) && number(spi, "gap_ns", d.spi.gapNs) &&
           number(spi, "manual_start", d.spi.manualStart) && number(lin, "baud", d.lin.baud) &&
           named(lin, "checksum", d.lin.enhanced, checksums) && lengths(lin, d.lin) && number(can, "bitrate", d.can.bitrate) &&
           small(can, "sample_point", d.can.samplePointPermille) && named(can, "source", d.can.source, canSources) &&
           number(can, "difference_mv", d.can.differenceMv) && number(can, "data_bitrate", d.can.dataBitrate) &&
           small(can, "data_sample_point", d.can.dataSamplePointPermille) && rs485Of(object, d.rs485);
}
void decoder(JsonObjectConst object, decoding::Settings &output) {
    using decoding::Protocols;
    decoding::Settings d;
    uint32_t protocol = uint32_t(d.protocol), view = uint32_t(d.view), update = uint32_t(d.update);
    // A file from before the Auto hysteresis with its hysteresis_mv: that value stays in force (Manual).
    uint32_t hysteresis = uint32_t(object.containsKey("hysteresis_mv") ? decoding::Hysteresis::Manual : d.hysteresis);
    static constexpr const char *protocols[] = {"off", "uart", "i2c", "spi", "lin", "can", "rs232", "rs485", "dmx512"};
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
    if (d.uart.rs232ThresholdMv) uart["rs232_threshold_mv"] = d.uart.rs232ThresholdMv;
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
    if (d.rs485 == decoding::Rs485Settings()) return; // Written only once changed: the file's budget.
    auto rs485 = object.createNestedArray("rs485");
    rs485.add(d.rs485.thresholdMv); rs485.add(rs485Sources[unsigned(d.rs485.source)]); rs485.add(bool(d.rs485.inverted));
}
// scope.bode (GitHub #124): the Bode setup, written only when it differs from the defaults. Each field
// stands alone (as scope.keys): a missing or damaged one keeps its default, the rest of the file kept.
// The frequencies in hertz, one of BodePreferences::Steps, or "limit" (the drive's highest sine); the
// lists by their values; the Custom drive's codes as a [low, high] pair.
constexpr const char *bodeDrives[] = {"gen", "dedicated", "low", "high", "custom"}; // ui::BodeDrive.
static_assert(sizeof bodeDrives / sizeof *bodeDrives == BodePreferences::Drives);
using BodeChoices = scope::BodeSettings;
static_assert(sizeof BodeChoices::PointsChoices / sizeof *BodeChoices::PointsChoices == BodePreferences::Densities &&
              sizeof BodeChoices::SettleChoices / sizeof *BodeChoices::SettleChoices == BodePreferences::Settles &&
              sizeof BodeChoices::AverageChoices / sizeof *BodeChoices::AverageChoices == BodePreferences::Averages);
void bodeFrequency(JsonObjectConst object, const char *name, uint8_t &index) {
    const auto value = object[name];
    if (value.is<const char *>() && !std::strcmp(value.as<const char *>(), "limit")) { index = BodePreferences::Limit; return; }
    if (!value.is<uint32_t>()) return;
    for (uint8_t i = 0; i < BodePreferences::StepCount; ++i)
        if (BodePreferences::Steps[i] == uint64_t(value.as<uint32_t>()) * 1000) { index = i; return; }
}
template<size_t N> void bodeChoice(JsonObjectConst object, const char *name, uint8_t &index, const uint32_t (&values)[N]) {
    if (!object[name].is<uint32_t>()) return;
    for (uint8_t i = 0; i < N; ++i) if (values[i] == object[name].as<uint32_t>()) { index = i; return; }
}
void bode(JsonObjectConst object, BodePreferences &output) {
    BodePreferences b;
    bodeFrequency(object, "start_hz", b.start);
    bodeFrequency(object, "stop_hz", b.stop);
    bodeChoice(object, "points_per_decade", b.density, BodeChoices::PointsChoices);
    bodeChoice(object, "settle_ms", b.settle, BodeChoices::SettleChoices);
    bodeChoice(object, "averages", b.averages, BodeChoices::AverageChoices);
    option(object, "drive", b.drive, bodeDrives);
    const auto codes = object["custom_codes"].as<JsonArrayConst>();
    if (codes.size() == 2 && codes[0].is<uint32_t>() && codes[1].is<uint32_t>() &&
        BodePreferences::codes(codes[0].as<uint32_t>(), codes[1].as<uint32_t>())) {
        b.customLow = uint16_t(codes[0].as<uint32_t>()); b.customHigh = uint16_t(codes[1].as<uint32_t>());
    }
    output = b;
}
void encodeBode(JsonObject scope, const BodePreferences &b) {
    if (b == BodePreferences{}) return;
    auto object = scope.createNestedObject("bode");
    const auto frequency = [&](const char *name, uint8_t index) {
        if (index < BodePreferences::StepCount) object[name] = uint32_t(BodePreferences::Steps[index] / 1000);
        else object[name] = "limit";
    };
    frequency("start_hz", b.start); frequency("stop_hz", b.stop);
    object["points_per_decade"] = BodeChoices::PointsChoices[b.density];
    object["settle_ms"] = BodeChoices::SettleChoices[b.settle];
    object["averages"] = BodeChoices::AverageChoices[b.averages];
    object["drive"] = bodeDrives[b.drive];
    auto codes = object.createNestedArray("custom_codes"); codes.add(b.customLow); codes.add(b.customHigh);
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
    uint32_t legacyInterleave = 0;
    if (!boolean(scope, "interleave", legacyInterleave)) return false;
    // interleave_mode (InterleaveMode: 0 Off, 1 On, 2 Auto). Without it: the ZT706S Auto (its old
    // boolean was ignored), the ZT703S the old boolean (Off or On).
    s.interleave = board::Current::Features.adcInterleave ? 2 : legacyInterleave;
    if (!number(scope, "interleave_mode", s.interleave)) return false;
    if (!(scope.containsKey("run_mode") ? number(scope,"run_mode",s.runMode) : boolean(scope,"high_performance",s.runMode)) || !boolean(scope,"roll_auto",s.rollAuto) ||
        !boolean(scope, "longer_record", s.longerRecord) ||
        !choice(scope, "factory_record_format", s.factoryRecordFormat, factoryRecordFormats) ||
        !choice(meter, "display", output.meter.display, displays) ||
        !boolean(meter, "statistics", output.meter.statistics) || !boolean(meter, "bar_graph", output.meter.bar) ||
        !boolean(meter, "bar_scale_unit", output.meter.barUnit) || !boolean(meter,"auto",output.meter.automatic) ||
        !number(meter,"range_index",output.meter.range) || !choice(meter, "face", output.meter.face, faces) ||
        !choice(meter, "palette", output.meter.palette, palettes) || !choice(meter, "appearance_preset", output.meter.preset, presets)) return false;
    if (meter["appearance"].is<JsonObjectConst>()) appearance(meter["appearance"].as<JsonObjectConst>(), output.meter.appearance);
    // Files before the preset (GitHub #68): choosing Hybrid wrote its look over the Custom one. That exact
    // look is Hybrid, its Custom the base (the one before it is gone); any other look stays the Custom.
    // Only here: a file with the key keeps a Custom equal to Hybrid as Custom.
    auto &look = output.meter;
    if (!meter.containsKey("appearance_preset") && look.palette == MeterAppearanceCatalog::HybridPalette &&
        look.face == MeterAppearanceCatalog::HybridFace && look.appearance == MeterAppearanceCatalog::hybrid()) {
        look.preset = MeterPreferences::HybridPreset;
        look.appearance = MeterAppearance();
    }
    if (meter["hold"].is<JsonObjectConst>()) hold(meter["hold"].as<JsonObjectConst>(), output.meter.hold);
    if (meter["log"].is<JsonArrayConst>()) sessionLog(meter["log"].as<JsonArrayConst>(), output.meter.log);
    if (const auto temperature = meter["temperature"].as<JsonObjectConst>(); !temperature.isNull()) {
        dmm::TemperatureSettings settings;
        uint32_t fahrenheit = 0, mcuSensor = 0;
        if (number(temperature, "junction_deci_c", settings.junctionDeciC) &&
            boolean(temperature, "fahrenheit", fahrenheit) && settings.valid()) {
            boolean(temperature, "mcu_sensor", mcuSensor);
            // The old MCU option followed the sensor live. A named source takes precedence.
            uint32_t source = unsigned(mcuSensor ? dmm::JunctionSource::McuLive : dmm::JunctionSource::Manual);
            choice(temperature, "source", source, junctionSources);
            settings.fahrenheit = fahrenheit != 0;
            settings.source = dmm::JunctionSource(source);
            output.meter.temperature = settings;
        }
    }
    arrangement(meter, "arrangement_digital", output.meter.digitalArrangement);
    arrangement(meter, "arrangement_analog", output.meter.analogArrangement);
    arrangement(scope, "arrangement", s.arrangement);
    if (scope["appearance"].is<JsonObjectConst>()) ScopeAppearanceJson::parse(scope["appearance"].as<JsonObjectConst>(), s.appearance);
    if (scope["decoder"].is<JsonObjectConst>()) decoder(scope["decoder"].as<JsonObjectConst>(), s.decoder);
    if (scope["keys"].is<JsonObjectConst>()) keys(scope["keys"].as<JsonObjectConst>(), s.keys);
    if (scope["bode"].is<JsonObjectConst>()) bode(scope["bode"].as<JsonObjectConst>(), output.bode);
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
            !choice(trigger, "edge", s.triggerEdge, edges) || !choice(trigger, "mode", s.triggerMode, triggerModes) ||
            !choice(trigger, "type", s.triggerType, triggerTypes) || !choice(trigger, "condition", s.triggerCondition, triggerConditions) ||
            !number(trigger, "level_2_codes", s.triggerLevel2) || !number(trigger, "minimum_ns", s.triggerMinimumNs) ||
            !number(trigger, "maximum_ns", s.triggerMaximumNs)) return false;
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
    if (scope.containsKey("measure_short_window_counter")) { // GitHub #171; absent (older files): Off.
        if (!scope["measure_short_window_counter"].is<bool>()) return false;
        s.measurements.shortWindowCounter=scope["measure_short_window_counter"].as<bool>();
    }
    if (legacy) s.recordPoints = scope::RecordBudget::covering(legacySamples, scope::RecordBudget::channels(s.enabled[0], s.enabled[1]));
    return s.valid() && output.meter.valid();
}
void InstrumentJson::encode(JsonObject scope, JsonObject meter, const Preferences &values) {
    const auto &s = values.scope;
    scope["record_points"] = s.recordPoints;
    scope["run_mode"] = s.runMode;
    scope["roll_auto"]=bool(s.rollAuto);
    if (!s.longerRecord) scope["longer_record"] = false;
    scope["interleave"]=s.interleave == 1;
    // Both: older ZT703S firmware reads only the boolean, so Auto reaches it as Off.
    if (board::Current::Features.interleaveControl()) scope["interleave_mode"] = s.interleave;
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
    trigger["type"] = triggerTypes[s.triggerType]; trigger["condition"] = triggerConditions[s.triggerCondition];
    trigger["level_2_codes"] = s.triggerLevel2;
    trigger["minimum_ns"] = s.triggerMinimumNs; trigger["maximum_ns"] = s.triggerMaximumNs;
    auto measurements = scope.createNestedArray("measurements");
    for (const auto &channel : s.measurements.values) {
        auto row=measurements.createNestedArray();
        for (auto value : channel) row.add(value);
    }
    scope["measure_basis"] = s.measurements.basis==scope::ReadoutBasis::Record ? "record" : "screen";
    if (s.measurements.shortWindowCounter) scope["measure_short_window_counter"] = true; // Only On.
    arrangement(scope, "arrangement", s.arrangement);
    ScopeAppearanceJson::encode(scope, s.appearance);
    encodeDecoder(scope, s.decoder);
    auto keys = scope.createNestedObject("keys");
    keys["auto"] = autoKeys[unsigned(s.keys.automatic)]; keys["f2_hold"] = resetHolds[unsigned(s.keys.reset)];
    keys["f4_hold"] = shortcuts[unsigned(s.keys.destination())];
    encodeBode(scope, values.bode);
    meter["display"] = displays[values.meter.display]; meter["statistics"] = bool(values.meter.statistics); meter["bar_graph"] = bool(values.meter.bar);
    meter["bar_scale_unit"] = values.meter.barUnit;
    meter["auto"] = bool(values.meter.automatic); meter["range_index"] = values.meter.range;
    meter["face"] = faces[values.meter.face]; meter["palette"] = palettes[values.meter.palette];
    meter["appearance_preset"] = presets[values.meter.preset]; // Always: without it a Custom equal to Hybrid would migrate.
    arrangement(meter, "arrangement_digital", values.meter.digitalArrangement);
    arrangement(meter, "arrangement_analog", values.meter.analogArrangement);
    if (values.meter.rangeFunction < MeterFunctionCount) meter["range_function"] = MeterFunctions[values.meter.rangeFunction].key;
    appearance(meter, values.meter.appearance);
    hold(meter, values.meter.hold);
    sessionLog(meter, values.meter.log);
    const auto &temperature = values.meter.temperature;
    if (!(temperature == dmm::TemperatureSettings{})) {
        auto object = meter.createNestedObject("temperature");
        object["junction_deci_c"] = temperature.junctionDeciC;
        object["fahrenheit"] = temperature.fahrenheit;
        if (temperature.source != dmm::JunctionSource::Manual)
            object["source"] = junctionSources[unsigned(temperature.source)];
    }
}
} // namespace openosc
