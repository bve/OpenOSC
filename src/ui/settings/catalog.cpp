#include "openosc/ui/settings/menu.hpp"
#include "openosc/board/current.hpp"

namespace openosc::ui {
namespace {
using Page = SettingsPage;
using Action = SettingsAction;
constexpr SettingsEntry root[] = {
    {"Display", Action::None, Page::Display},
    {"Files & USB", Action::None, Page::Files},
    {"Device", Action::None, Page::Device},
    {"Back", Action::Back},
};
constexpr SettingsEntry display[] = {
    {"Brightness", Action::Brightness},
    {"Top bar counter", Action::ShowFps},
    {"Battery", Action::BatteryDisplay},
    {"Pan direction", Action::PanDirection},
    {"Generator layout", Action::GeneratorLayout},
    {"Appearance", Action::None, Page::Appearance},
    {"Back", Action::Back},
};
constexpr SettingsEntry appearance[] = {
    {"Reload theme JSON", Action::ReloadTheme},
    {"Use built-in theme", Action::BuiltinTheme},
    {"Back", Action::Back},
};
constexpr SettingsEntry files[] = {
    {"Reload settings JSON", Action::ReloadPreferences},
    {"Recovery / USB files", Action::Recovery},
    {"USB controller", Action::UsbController},
    {"USB Remote", Action::UsbRemote},
    {"Screenshots",Action::Images},
    {"Clear screenshots",Action::ClearImages},
    {"Back", Action::Back},
};
constexpr SettingsEntry device[] = {
    {"About", Action::About, Page::About},
    {"Storage", Action::Storage, Page::Storage},
    {"Performance", Action::None, Page::Performance},
    {"Power", Action::None, Page::Power},
    {"Restore settings",Action::RestoreSettings},
    {"Scope calibration",Action::None,Page::Calibration},
    {"Back", Action::Back},
};
constexpr SettingsEntry performance[] = {
    {"Run mode", Action::RunProfile},
    {"Record memory", Action::BufferSize}, // Total points of the enabled channels.
    {"Restart device", Action::Restart},
    {"Back", Action::Back},
};
// The interleave choice sits next to the run mode it uses.
constexpr SettingsEntry selectablePerformance[] = {
    {"Run mode", Action::RunProfile},
    {"Interleave CH1", Action::Interleave},
    {"Record memory", Action::BufferSize},
    {"Restart device", Action::Restart},
    {"Back", Action::Back},
};
constexpr SettingsEntry power[] = {
    {"Backlight timeout",Action::BacklightTimeout},
    {"Idle backlight",Action::BacklightIdleLevel},
    {"Auto power off",Action::PowerOffTimeout},
    {"Power-off hold",Action::HoldTime},
    {"Generator at startup",Action::GeneratorStartup},
    {"Back",Action::Back},
};
constexpr SettingsEntry calibration[] = {
    {"Hardware zero (PWM)",Action::CalibrateHardwareZero},
    {"Software zero",Action::CalibrateZero},
    {"Gain",Action::None,Page::Gain},
    {"Interleave offset",Action::CalibrateInterleave}, // The ZT703S's, per clock (the ZT706S page has its own).
    {"Interleave gain & timing",Action::CalibrateInterleaveMatch},
    {"Reload calibration JSON",Action::ReloadZero},
    {"Use factory zero",Action::FactoryZero},
    {"Back",Action::Back},
};
// The ZT706S's (interleaved CH1): the same page with its service offset.
constexpr SettingsEntry interleaveCalibration[] = {
    {"Hardware zero (PWM)",Action::CalibrateHardwareZero},
    {"Software zero",Action::CalibrateZero},
    {"Gain",Action::None,Page::Gain},
    {"Interleave offset",Action::None,Page::Interleave},
    {"Reload calibration JSON",Action::ReloadZero},
    {"Use factory zero",Action::FactoryZero},
    {"Back",Action::Back},
};
constexpr SettingsEntry gain[] = {
    {"Channel",Action::GainChannel},
    {"V/div",Action::GainRange},
    {"Reference at tip",Action::GainReference},
    {"Reference step",Action::GainReferenceStep},
    {"Probe",Action::GainProbe},
    {"Expected span",Action::GainSpan},
    {"Gain in force",Action::GainInForce},
    {"Start measurement",Action::CalibrateGain},
    {"Use factory gain",Action::FactoryGain},
    {"Back",Action::Back},
};
constexpr SettingsEntry interleave[] = {
    {"Offset",Action::InterleaveOffset},
    {"In force",Action::InterleaveInForce},
    {"Saved",Action::InterleaveSaved},
    {"Save",Action::SaveInterleave},
    {"Cancel",Action::CancelInterleave},
    {"Reset to factory",Action::ResetInterleave},
    {"Back",Action::Back},
};
template <size_t N>
constexpr SettingsPageDefinition page(const char *title, const char *path, Page parent,
                                      const SettingsEntry (&entries)[N]) {
    return {title, path, parent, entries, N};
}
constexpr SettingsPageDefinition pages[] = {
    page("SETTINGS", "Settings", Page::Root, root),
    page("DISPLAY", "Settings / Display", Page::Root, display),
    page("APPEARANCE", "Settings / Display / Appearance", Page::Display, appearance),
    page("FILES & USB", "Settings / Files & USB", Page::Root, files),
    page("DEVICE", "Settings / Device", Page::Root, device),
    {"ABOUT", "Settings / Device / About", Page::Device, nullptr, 0},
    {"STORAGE", "Settings / Device / Storage", Page::Device, nullptr, 0},
    board::Current::Features.interleaveControl()
        ? page("PERFORMANCE", "Settings / Device / Performance", Page::Device, selectablePerformance)
        : page("PERFORMANCE", "Settings / Device / Performance", Page::Device, performance),
    page("POWER", "Settings / Device / Power", Page::Device, power),
    board::Current::Features.adcInterleave
        ? page("SCOPE CALIBRATION","Settings / Device / Scope calibration",Page::Device,interleaveCalibration)
        : page("SCOPE CALIBRATION","Settings / Device / Scope calibration",Page::Device,calibration),
    page("GAIN","Settings / Device / Scope calibration / Gain",Page::Calibration,gain),
    page("INTERLEAVE OFFSET","Settings / Device / Scope calibration / Interleave",Page::Calibration,interleave),
};
static_assert(sizeof pages / sizeof *pages == unsigned(Page::Count));
}
const SettingsPageDefinition &SettingsMenu::definition(SettingsPage page) {
    return pages[unsigned(page)];
}
} // namespace openosc::ui
