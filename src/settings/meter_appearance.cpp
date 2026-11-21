#include "openosc/settings/meter_appearance.hpp"
#include <cstring>

namespace openosc {
namespace {
constexpr const char *readingFonts[] = {"IBM", "Mono", "Segment"}, *readingKeys[] = {"ibm", "mono", "segment"};
constexpr const char *textFonts[] = {"IBM", "Mono"}, *textKeys[] = {"ibm", "mono"};
constexpr const char *lines[] = {"Thin", "Bold"}, *lineKeys[] = {"thin", "bold"};
constexpr MeterRoleInfo color(const char *name, const char *key) { return {name, key, 0, nullptr, nullptr, 0, nullptr, nullptr}; }
constexpr MeterRoleInfo line(const char *name, const char *key) { return {name, key, 0, nullptr, nullptr, 2, lines, lineKeys}; }
constexpr MeterRoleInfo text(const char *name, const char *key) { return {name, key, 2, textFonts, textKeys, 0, nullptr, nullptr}; }
constexpr MeterRoleInfo roles[MeterAppearance::Roles] = {
    color("Background", "background"),
    {"Reading", "reading", 3, readingFonts, readingKeys, 0, nullptr, nullptr},
    text("Unit", "unit"),
    color("Function sign", "function"),
    line("Dial arc", "arc"), line("Dial ticks", "ticks"), text("Scale numbers", "scale_numbers"), line("Needle", "needle"),
    text("Stat names", "stat_names"), text("MIN value", "minimum"), text("MAX value", "maximum"), text("AVG value", "average"),
    text("Range / REL", "range"), text("Secondary", "secondary"), text("AUTO / MANUAL", "mode"), text("Info line", "details"),
    color("Bar / trend", "bar"),
    color("Card sign lit", "card_active"), color("Card signs", "card_inactive"), text("Card text", "card_text"),
    color("Card frame", "card_selection")};
} // namespace
uint8_t MeterAppearanceCatalog::find(const char *const *keys, unsigned count, const char *key) {
    for (unsigned i = 0; key && i < count; ++i) if (!std::strcmp(keys[i], key)) return uint8_t(i);
    return MeterRoleStyle::Inherit;
}
uint8_t MeterAppearanceCatalog::swatchByKey(const char *key) { return ColorCatalog::byKey(key); }
const MeterSwatch &MeterAppearanceCatalog::swatch(unsigned index) { return ColorCatalog::swatch(index); }
const MeterRoleInfo &MeterAppearanceCatalog::role(MeterRole role) { return roles[unsigned(role) < MeterAppearance::Roles ? unsigned(role) : 0]; }
bool MeterAppearance::custom() const {
    for (const auto &role : roles) if (!(role == MeterRoleStyle{})) return true;
    return false;
}
bool MeterAppearance::valid() const {
    for (unsigned i = 0; i < Roles; ++i) {
        const auto &r = roles[i];
        const auto &info = MeterAppearanceCatalog::role(MeterRole(i));
        if (!r.colorValue().valid() ||
            (r.font != MeterRoleStyle::Inherit && r.font >= info.fonts) || (r.style != MeterRoleStyle::Inherit && r.style >= info.styles))
            return false;
    }
    return true;
}
bool MeterAppearance::operator==(const MeterAppearance &b) const {
    for (unsigned i = 0; i < Roles; ++i) if (!(roles[i] == b.roles[i])) return false;
    return true;
}
namespace {
// The Hybrid look, built at compile time: a constant in flash, handed out by reference (no RAM copy).
constexpr MeterAppearance makeHybrid() {
    using C = ColorCatalog;
    using Catalog = MeterAppearanceCatalog;
    MeterAppearance a;
    const auto set = [&a](MeterRole role, uint8_t swatch) { a.roles[unsigned(role)].color = swatch; };
    set(MeterRole::Background, C::Black);
    set(MeterRole::Reading, C::Amber); set(MeterRole::Unit, C::Amber);
    a.roles[unsigned(MeterRole::Reading)].font = Catalog::ReadingIbm; // IBM, whatever the face.
    a.roles[unsigned(MeterRole::Unit)].font = Catalog::TextIbm;
    set(MeterRole::Function, C::White);
    set(MeterRole::Arc, C::White); set(MeterRole::Ticks, C::White); set(MeterRole::ScaleNumbers, C::White);
    set(MeterRole::Needle, C::Orange); a.roles[unsigned(MeterRole::Needle)].style = Catalog::Bold;
    set(MeterRole::StatNames, C::Gray);
    set(MeterRole::Minimum, C::Silver); set(MeterRole::Maximum, C::Silver); set(MeterRole::Average, C::Silver);
    set(MeterRole::Range, C::Gray); set(MeterRole::Secondary, C::Silver); set(MeterRole::Mode, C::Sky); set(MeterRole::Details, C::Gray);
    set(MeterRole::Bar, C::Amber);
    set(MeterRole::CardActive, C::Cyan); set(MeterRole::CardInactive, C::Gray); set(MeterRole::CardText, C::Silver);
    set(MeterRole::CardSelection, C::Cyan);
    return a;
}
constexpr MeterAppearance Hybrid = makeHybrid();
} // namespace
const MeterAppearance &MeterAppearanceCatalog::hybrid() { return Hybrid; }
} // namespace openosc
