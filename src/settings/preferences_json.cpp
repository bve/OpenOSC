#include "openosc/settings/preferences_json.hpp"
#include "openosc/settings/arrangement_json.hpp"
#include "openosc/settings/json_input.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "openosc/settings/instrument_json.hpp"
#include "openosc/acquisition/timebases.hpp"
#include <ArduinoJson.h>
#include <cstring>

namespace openosc {
namespace {
constexpr size_t DocumentBytes = JSON_OBJECT_SIZE(PreferencesJson::DocumentSlots) + PreferencesJson::StringBytes;
constexpr const char *generatorShapes[] = {"sine",      "square",        "triangle",
                                           "half_sine", "positive_sine", "sawtooth"};
constexpr const char *generatorLevels[] = {"dedicated","low","high","custom"};
// A Dedicated square's drive (generator::SquareDrive); absent in older files: factory.
constexpr const char *generatorDrives[] = {"factory","pwm"};
// Startup output (GeneratorStartup); absent in older files: off.
constexpr const char *generatorStartups[] = {"off","restore"};
constexpr const char *batteryDisplays[] = {"icon","volts","percent"};
// usb.controller (Preferences::usbController, the index); absent in older files: enabled.
constexpr const char *usbControllers[] = {"disabled","enabled"};
template<class T> bool number(JsonObjectConst object, const char *key, T &value) {
    if (!object.containsKey(key))
        return true;
    if (!object[key].is<T>())
        return false;
    value = object[key].as<T>();
    return true;
}
bool section(JsonObjectConst root, const char *key) {
    return !root.containsKey(key) || root[key].is<JsonObjectConst>();
}
} // namespace
bool PreferencesJson::parse(const char *input, size_t bytes, Preferences &output) {
    if (bytes > MaxBytes || !JsonInput::completeObject(input, bytes, 6))
        return false;
    JsonWorkspace::Document<DocumentBytes> lease; // Off the stack (JsonWorkspace).
    if (!lease || deserializeJson(*lease, input, bytes, DeserializationOption::NestingLimit(6)))
        return false;
    JsonObjectConst root = lease->as<JsonObjectConst>();
    if (!root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1 || !section(root, "display") ||
        !section(root, "scope") || !section(root, "multimeter") || !section(root, "generator") || !section(root, "usb") ||
        !section(root, "power"))
        return false;
    Preferences candidate;
    if (!number(root["display"], "brightness", candidate.brightness) ||
        !number(root["display"],"backlight_timeout_s",candidate.idle.backlightSeconds) ||
        !number(root["display"],"backlight_idle_percent",candidate.idle.dimPercent) ||
        !number(root,"power_off_minutes",candidate.idle.powerOffMinutes))
        return false;
    const auto indicators = root["display"].as<JsonObjectConst>();
    if (indicators.containsKey("show_fps")) {
        if (!indicators["show_fps"].is<bool>()) return false;
        candidate.showFps = indicators["show_fps"].as<bool>();
    }
    if (indicators.containsKey("show_sample_rate")) {
        if (!indicators["show_sample_rate"].is<bool>()) return false;
        if (indicators["show_sample_rate"].as<bool>()) candidate.showFps = 2;
    }
    if (indicators.containsKey("clock_indicator")) { // Absent in older files: off.
        if (!indicators["clock_indicator"].is<bool>()) return false;
        candidate.clockIndicator = indicators["clock_indicator"].as<bool>();
    }
    if (indicators.containsKey("tooltips_off")) { // GitHub #108; absent in older files: every kind shown.
        if (!indicators["tooltips_off"].is<unsigned>() || !Tooltips::valid(indicators["tooltips_off"].as<unsigned>())) return false;
        candidate.tooltipsHidden = uint8_t(indicators["tooltips_off"].as<unsigned>());
    }
    if (indicators.containsKey("pan_moves_waveform")) {
        if (!indicators["pan_moves_waveform"].is<bool>()) return false;
        candidate.panReverse = indicators["pan_moves_waveform"].as<bool>();
    }
    if (root.containsKey("hold_ms")) {
        if (!root["hold_ms"].is<unsigned>()) return false;
        const unsigned ms = root["hold_ms"].as<unsigned>();
        unsigned index = 0;
        while (index < 4 && Preferences::HoldTimes[index] != ms) ++index;
        if (index == 4) return false;
        candidate.holdTime = index;
    }
    if (indicators.containsKey("battery")) {
        const char *name = indicators["battery"].is<const char *>() ? indicators["battery"].as<const char *>() : "";
        unsigned index = 0;
        while (index < 3 && std::strcmp(name, batteryDisplays[index])) ++index;
        if (index == 3) return false;
        candidate.batteryDisplay = index;
    }
    { // power: the battery display trim (GitHub #107); absent in older files: none.
        int gain = 0, offset = 0;
        const auto power = root["power"].as<JsonObjectConst>();
        if (!number(power, "battery_gain_permille", gain) || !number(power, "battery_offset_mv", offset) ||
            !power::BatteryGauge::trimValid(gain, offset)) return false;
        candidate.batteryGainPermille = int8_t(gain); candidate.batteryOffsetMv = int16_t(offset);
    }
    const auto usb = root["usb"].as<JsonObjectConst>();
    if (usb.containsKey("controller")) {
        const char *name = usb["controller"].is<const char *>() ? usb["controller"].as<const char *>() : "";
        unsigned index = 0;
        while (index < 2 && std::strcmp(name, usbControllers[index])) ++index;
        if (index == 2) return false;
        candidate.usbController = index;
    }
    const auto timing = root["scope"].as<JsonObjectConst>();
    if (timing.containsKey("timebase_ns")) {
        if (timing.containsKey("timebase_us") || !number(timing,"timebase_ns",candidate.timebaseNs)) return false;
    } else if (timing.containsKey("timebase_us")) {
        uint32_t microseconds=0;
        if (!number(timing,"timebase_us",microseconds)) return false;
        candidate.timebaseNs=uint64_t(microseconds)*1000;
    }
    if (candidate.timebaseNs >= 10 && candidate.timebaseNs <= 20000000000ULL)
        candidate.timebaseNs = scope::Timebases::nearest(double(candidate.timebaseNs));
    if (root.containsKey("mode")) {
        if (!root["mode"].is<const char *>())
            return false;
        const char *mode = root["mode"];
        unsigned index = 0;
        while (index < InstrumentModeCount && std::strcmp(mode, InstrumentModeKeys[index]))
            ++index;
        if (index == InstrumentModeCount)
            return false;
        candidate.mode = index;
    }
    auto display = root["display"].as<JsonObjectConst>();
    if (display.containsKey("theme")) {
        if (!display["theme"].is<const char *>())
            return false;
        const char *theme = display["theme"];
        if (!std::strcmp(theme, "custom"))
            candidate.useBuiltinTheme = 0;
        else if (!std::strcmp(theme, "builtin"))
            candidate.useBuiltinTheme = 1;
        else
            return false;
    }
    auto scope = root["scope"].as<JsonObjectConst>();
    if (scope.containsKey("millivolts_per_division")) {
        auto channels = scope["millivolts_per_division"].as<JsonArrayConst>();
        if (channels.size() != 2 || !channels[0].is<uint32_t>() || !channels[1].is<uint32_t>())
            return false;
        candidate.millivolts[0] = channels[0];
        candidate.millivolts[1] = channels[1];
    }
    auto meter = root["multimeter"].as<JsonObjectConst>();
    if (meter.containsKey("function")) {
        if (!meter["function"].is<const char *>())
            return false;
        unsigned function = 0;
        while (function < MeterFunctionCount && std::strcmp(meter["function"], MeterFunctions[function].key))
            ++function;
        if (function == MeterFunctionCount)
            return false;
        candidate.meterFunction = function;
    }
    const auto generator = root["generator"].as<JsonObjectConst>();
    if (!number(generator, "frequency_millihz", candidate.generatorMillihertz) ||
        !number(generator, "duty_permille", candidate.generatorDutyPermille) ||
        !number(generator, "amplitude_mv", candidate.generatorAmplitudeMillivolts) ||
        !number(generator,"minimum_code",candidate.generatorMinimumCode) ||
        !number(generator,"maximum_code",candidate.generatorMaximumCode))
        return false;
    if (generator.containsKey("level")) {
        if (!generator["level"].is<const char *>()) return false;
        unsigned index=0;
        while (index<4 && std::strcmp(generator["level"],generatorLevels[index])) ++index;
        if (index==4) return false;
        candidate.generatorLevel=index;
    }
    if (generator.containsKey("square_drive")) {
        if (!generator["square_drive"].is<const char *>()) return false;
        unsigned index=0;
        while (index<2 && std::strcmp(generator["square_drive"],generatorDrives[index])) ++index;
        if (index==2) return false;
        candidate.generatorSquareDrive=index;
    }
    if (generator.containsKey("startup_output")) {
        if (!generator["startup_output"].is<const char *>()) return false;
        unsigned index=0;
        while (index<2 && std::strcmp(generator["startup_output"],generatorStartups[index])) ++index;
        if (index==2) return false;
        candidate.generatorStartup=index;
    }
    arrangement(generator, "arrangement", candidate.generatorArrangement); // Missing or damaged: the default.
    if (generator.containsKey("last_output")) {
        if (!generator["last_output"].is<bool>()) return false;
        candidate.generatorLastOutput=generator["last_output"].as<bool>();
    }
    if (generator.containsKey("shape")) {
        if (!generator["shape"].is<const char *>())
            return false;
        unsigned index = 0;
        while (index < 6 && std::strcmp(generator["shape"], generatorShapes[index]))
            ++index;
        if (index == 6)
            return false;
        candidate.generatorShape = index;
    }
    constexpr unsigned shapes = sizeof generator::ShapeNames / sizeof *generator::ShapeNames, levels = unsigned(generator::Level::Custom) + 1;
    if (candidate.generatorShape < shapes && candidate.generatorLevel < levels) { // A limit lowered since the save: clamped, the rest kept.
        const uint64_t limit = generator::Capabilities::maximumMillihertz(generator::Shape(candidate.generatorShape), generator::Level(candidate.generatorLevel));
        if (limit && candidate.generatorMillihertz > limit) candidate.generatorMillihertz = limit;
    }
    if (!InstrumentJson::parse(scope, meter, candidate) || !candidate.valid())
        return false;
    output = candidate;
    return true;
}
size_t PreferencesJson::encode(const Preferences &values, char *output, size_t capacity, bool pretty) {
    if (!values.valid())
        return 0;
    JsonWorkspace::Document<DocumentBytes> lease; // Off the stack (JsonWorkspace).
    if (!lease) return 0;
    auto &document = *lease;
    document["schema"] = 1;
    document["mode"] = InstrumentModeKeys[values.mode];
    document["display"]["brightness"] = values.brightness;
    document["display"]["backlight_timeout_s"]=values.idle.backlightSeconds;
    document["display"]["backlight_idle_percent"]=values.idle.dimPercent;
    document["power_off_minutes"]=values.idle.powerOffMinutes;
    document["display"]["theme"] = values.useBuiltinTheme ? "builtin" : "custom";
    document["display"]["show_fps"] = values.showFps == 1;
    document["display"]["show_sample_rate"] = values.showFps == 2;
    document["display"]["pan_moves_waveform"] = values.panReverse == 1;
    document["display"]["tooltips_off"] = unsigned(values.tooltipsHidden); // A bit per kind hidden (Tooltips).
    document["display"]["clock_indicator"] = values.clockIndicator == 1;
    document["hold_ms"] = values.holdMs();
    document["display"]["battery"] = batteryDisplays[values.batteryDisplay];
    document["power"]["battery_gain_permille"] = int(values.batteryGainPermille);
    document["power"]["battery_offset_mv"] = int(values.batteryOffsetMv);
    document["usb"]["controller"] = usbControllers[values.usbController];
    document["scope"]["timebase_ns"] = values.timebaseNs;
    auto channels = document["scope"].createNestedArray("millivolts_per_division");
    channels.add(values.millivolts[0]);
    channels.add(values.millivolts[1]);
    document["multimeter"]["function"] = MeterFunctions[values.meterFunction].key;
    InstrumentJson::encode(document["scope"], document["multimeter"], values);
    document["generator"]["shape"] = generatorShapes[values.generatorShape];
    document["generator"]["frequency_millihz"] = values.generatorMillihertz;
    document["generator"]["duty_permille"] = values.generatorDutyPermille;
    document["generator"]["amplitude_mv"] = values.generatorAmplitudeMillivolts;
    document["generator"]["level"] = generatorLevels[values.generatorLevel];
    document["generator"]["square_drive"] = generatorDrives[values.generatorSquareDrive];
    document["generator"]["startup_output"] = generatorStartups[values.generatorStartup];
    document["generator"]["last_output"] = bool(values.generatorLastOutput);
    arrangement(document["generator"].as<JsonObject>(), "arrangement", values.generatorArrangement);
    document["generator"]["minimum_code"] = values.generatorMinimumCode;
    document["generator"]["maximum_code"] = values.generatorMaximumCode;
    if (document.overflowed()) return 0;
    if (pretty && measureJsonPretty(document) >= capacity) pretty = false; // Every field, compact.
    if ((pretty ? measureJsonPretty(document) : measureJson(document)) >= capacity)
        return 0;
    return pretty ? serializeJsonPretty(document, output, capacity)
                  : serializeJson(document, output, capacity);
}
} // namespace openosc
