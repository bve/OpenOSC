#include "openosc/settings/scope_appearance.hpp"
#include <cstring>

namespace openosc {
namespace {
constexpr ScopeRoleInfo roles[ScopeAppearance::Roles] = {
    {"Background", "background"}, {"Panels", "panel"},      {"Text", "text"},
    {"Secondary text", "secondary"}, {"Grid", "grid"},      {"CH1", "channel1"},
    {"CH2", "channel2"},          {"Math", "math"},         {"Reference", "reference"},
    {"DMM", "dmm"},               {"Cursors", "cursor"},    {"Trigger", "trigger"},
    {"Selection", "selection"},   {"Readouts", "readout"},  {"Tag text", "tag_text"}};
constexpr const char *schemeNames[] = {"Current", "High contrast", "Daylight", "Amber"};
constexpr const char *schemeKeys[] = {"current", "high_contrast", "daylight", "amber"};
static_assert(sizeof schemeKeys / sizeof *schemeKeys == unsigned(ScopeScheme::Count));
} // namespace
bool ScopeAppearance::custom() const {
    for (const auto &role : roles) if (!role.inherit()) return true;
    return false;
}
bool ScopeAppearance::valid() const {
    if (unsigned(scheme) >= unsigned(ScopeScheme::Count)) return false;
    for (const auto &role : roles) if (!role.valid()) return false;
    return true;
}
bool ScopeAppearance::operator==(const ScopeAppearance &o) const {
    if (scheme != o.scheme) return false;
    for (unsigned i = 0; i < Roles; ++i) if (roles[i] != o.roles[i]) return false;
    return true;
}
const ScopeRoleInfo &ScopeAppearanceCatalog::role(ScopeRole role) { return roles[unsigned(role) < ScopeAppearance::Roles ? unsigned(role) : 0]; }
const char *ScopeAppearanceCatalog::schemeName(ScopeScheme scheme) { return schemeNames[unsigned(scheme) < unsigned(ScopeScheme::Count) ? unsigned(scheme) : 0]; }
const char *ScopeAppearanceCatalog::schemeKey(ScopeScheme scheme) { return schemeKeys[unsigned(scheme) < unsigned(ScopeScheme::Count) ? unsigned(scheme) : 0]; }
ScopeScheme ScopeAppearanceCatalog::schemeByKey(const char *key) {
    for (unsigned i = 0; key && i < unsigned(ScopeScheme::Count); ++i) if (!std::strcmp(schemeKeys[i], key)) return ScopeScheme(i);
    return ScopeScheme::Current;
}
ScopeRole ScopeAppearanceCatalog::roleByKey(const char *key) {
    for (unsigned i = 0; key && i < ScopeAppearance::Roles; ++i) if (!std::strcmp(roles[i].key, key)) return ScopeRole(i);
    return ScopeRole::Count;
}
} // namespace openosc
