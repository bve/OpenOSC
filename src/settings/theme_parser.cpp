#include "openosc/settings/theme_migration.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/settings/theme.hpp"
#include "openosc/settings/json_input.hpp"
#include "openosc/settings/json_workspace.hpp"
#include "openosc/settings/plot_json.hpp"
#include <cstring>

namespace openosc {
namespace {
bool color(JsonVariantConst value, uint32_t& output) {
    if (value.isNull()) return true;
    if (!value.is<const char*>()) return false;
    const char* s = value.as<const char*>();
    if (std::strlen(s) != 7 || *s++ != '#') return false;
    uint32_t result = 0;
    for (unsigned i = 0; i < 6; ++i) {
        const char c = s[i];
        unsigned nibble = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
        if (nibble > 15) return false;
        result = result * 16 + nibble;
    }
    output = result; return true;
}
bool dimension(JsonObjectConst obj, const char* name, int16_t& output, int min, int max) {
    if (!obj.containsKey(name)) return true;
    if (!obj[name].is<int>() || obj[name].as<int>() < min || obj[name].as<int>() > max) return false;
    output = obj[name].as<int>(); return true;
}
bool elements(JsonVariantConst input, ElementStyle* styles) {
    if (input.isNull()) return true;
    if (!input.is<JsonArrayConst>() || input.size() > ThemeInputElementLimit) return false;
    uint32_t seen = 0;
    for (JsonObjectConst obj : input.as<JsonArrayConst>()) {
        if (!obj["id"].is<const char*>()) return false;
        if (!std::strcmp(obj["id"], "mode")) {
            if (seen & (1U << ElementCount)) return false;
            seen |= 1U << ElementCount;
            continue; // Do not recreate instrument titles from an older theme.
        }
        unsigned id = 0;
        while (id < ElementCount && std::strcmp(obj["id"], ElementNames[id])) ++id;
        if (id == ElementCount || (seen & (1U << id))) return false;
        seen |= 1U << id;
        auto& e = styles[id];
        if (!dimension(obj, "x", e.x, 0, 319) || !dimension(obj, "y", e.y, 0, 239)
            || !dimension(obj, "width", e.width, 1, 320) || !dimension(obj, "height", e.height, 1, 240)
            || e.x + e.width > 320 || e.y + e.height > 240 || !color(obj["color"], e.color)) return false;
        if (obj.containsKey("visible")) {
            if (!obj["visible"].is<bool>()) return false;
            e.visible = obj["visible"];
        }
        if (obj.containsKey("font")) {
            if (!obj["font"].is<unsigned>()) return false;
            const unsigned f = obj["font"];
            if (f != 12 && f != 14 && f != 24 && f != 32 && f != 40 && f != 56 && f != 64) return false;
            e.font = f;
        }
        if (obj.containsKey("z")) {
            if (!obj["z"].is<unsigned>() || obj["z"].as<unsigned>() > ElementZOrderMax) return false;
            e.zOrder = obj["z"];
        }
        if (obj.containsKey("icon")) {
            if (id != unsigned(ElementId::Icon) || !obj["icon"].is<const char*>()) return false;
            const char* p = obj["icon"];
            if (std::strncmp(p, storage::Layout::IconPrefix, std::strlen(storage::Layout::IconPrefix)) || std::strstr(p, "..") || std::strlen(p) >= sizeof e.icon) return false;
            std::strcpy(e.icon, p);
        }
    }
    return true;
}
}
bool ThemeParser::parse(const char* json, size_t bytes, Theme& output) {
    if (!json || bytes > MaxBytes || !JsonInput::completeObject(json, bytes, 8)) return false;
    JsonWorkspace::Document<ThemeDocumentCapacity> document;
    if (!document || deserializeJson(*document, json, bytes, DeserializationOption::NestingLimit(8))) return false;
    JsonObjectConst root = document->as<JsonObjectConst>();
    if (!root["schema"].is<unsigned>() || root["schema"].as<unsigned>() != 1) return false;
    bool modernElements = false;
    for (const char *screen : ThemeScreenKeys)
        for (JsonObjectConst element : root[screen].as<JsonArrayConst>()) {
            const char *id = element["id"] | "";
            for (unsigned i = 6; i < ElementCount; ++i)
                modernElements |= !std::strcmp(id, ElementNames[i]);
        }
    LayoutPreset layout = modernElements ? LayoutPreset::Cards : LayoutPreset::Legacy;
    const bool explicitLayout = root.containsKey("layout");
    if (explicitLayout) {
        if (!root["layout"].is<const char *>()) return false;
        const char *name = root["layout"];
        if (!std::strcmp(name, "cards")) layout = LayoutPreset::Cards;
        else if (!std::strcmp(name, "compact")) layout = LayoutPreset::Compact;
        else if (!std::strcmp(name, "legacy")) layout = LayoutPreset::Legacy;
        else return false;
    }
    // Whole themes off the stack (JsonWorkspace): the candidate and, below, the stock reference.
    JsonWorkspace::ThemeLease candidateSlot(JsonWorkspace::ThemeSlot::Candidate), referenceSlot(JsonWorkspace::ThemeSlot::Reference);
    if (!candidateSlot || !referenceSlot) return false;
    Theme &candidate = candidateSlot.make([layout] { return Theme::defaults(layout); });
    if(root.containsKey("plot")&&(!root["plot"].is<JsonObjectConst>()||!PlotJson::parse(root["plot"],candidate.plot)))return false;
    const auto colors = root["colors"];
    if (!colors.isNull() && !colors.is<JsonObjectConst>()) return false;
    // Old generated files wrote all twelve palette entries, including the stock MATH in CH2's sky (#184).
    // Match that complete palette so an explicit MATH colour in a custom file keeps its meaning.
    bool legacyStock = !root.containsKey("palette_revision") && colors.size() == 12;
    const auto paletteColor = [&](const char *name, uint32_t &value) {
        const uint32_t stock = value;
        const auto entry = colors[name];
        if (!color(entry, value)) return false;
        legacyStock &= !entry.isNull() && value == stock;
        return true;
    };
    if (!paletteColor("background", candidate.background) || !paletteColor("foreground", candidate.foreground)
        || !paletteColor("accent", candidate.accent) || !paletteColor("grid", candidate.grid)
        || !paletteColor("channel1", candidate.channel1) || !paletteColor("channel2", candidate.channel2)
        || !paletteColor("panel", candidate.panel) || !paletteColor("muted", candidate.muted)
        || !paletteColor("generator", candidate.generator) || !paletteColor("reference", candidate.reference)) return false;
    candidate.math = candidate.accent == candidate.channel1 || candidate.accent == candidate.channel2
                     ? Theme::StockMath : candidate.accent;
    candidate.cursor = candidate.foreground;
    const auto math = colors["math"];
    if (!paletteColor("cursor", candidate.cursor) || !color(math, candidate.math)) return false;
    if (legacyStock && !math.isNull() && candidate.math == 0x38bdf8) candidate.math = Theme::StockMath;
    for (auto& screen : candidate.elements) for (auto& e : screen)
        e.color = e.color == 0x38bdf8 ? candidate.accent
                  : e.color == 0xfacc15 ? candidate.channel1
                  : e.color == 0xfb923c ? candidate.generator : candidate.foreground;
    for (unsigned mode = 0; mode < InstrumentModeCount; ++mode)
        if (!elements(root[ThemeScreenKeys[mode]], candidate.elements[mode])) return false;
    // An older firmware's stock DMM body (fonts included) first: the legacy check below then sees it as stock.
    ThemeMigration::adoptStockMeter(candidate);
    if (!explicitLayout && !modernElements) {
        const Theme &old = referenceSlot.make([] { return Theme::defaults(LayoutPreset::Legacy); });
        bool custom = false;
        for (unsigned mode = 0; mode < InstrumentModeCount; ++mode)
            for (unsigned i = 0; i < 6; ++i) {
                custom |= !ThemeMigration::legacyStock(mode, i, candidate.elements[mode][i], old.elements[mode][i]);
            }
        if (!custom) {
            const Theme &updated = referenceSlot.make([] { return Theme::defaults(); }); // `old` ends here.
            for (unsigned mode = 0; mode < InstrumentModeCount; ++mode)
                for (unsigned i = 0; i < ElementCount; ++i) {
                    auto &e = candidate.elements[mode][i];
                    const auto &next = updated.elements[mode][i];
                    e.x = next.x; e.y = next.y; e.width = next.width; e.height = next.height;
                    if (i >= 6) e.visible = next.visible;
                }
            candidate.layout = LayoutPreset::Cards;
        }
    }
    ThemeMigration::adoptStockScope(candidate); // An older firmware's stock Scope geometry.
    output = candidate;
    return true;
}
}
