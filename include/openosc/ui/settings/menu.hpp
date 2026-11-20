#pragma once
#include <cstddef>
#include <cstdint>

namespace openosc::ui {
enum class SettingsPage : uint8_t { Root, Display, Appearance, Files, Device, About, Storage, Performance, Power, Calibration, Gain, Interleave, Count };
enum class SettingsAction : uint8_t {
    None, Close, Back, Brightness, ReloadPreferences, ReloadTheme, BuiltinTheme, About, Storage, Recovery, UsbRemote,
    RunProfile, Restart, BacklightTimeout, BacklightIdleLevel, PowerOffTimeout, RestoreSettings, Images, ClearImages,
    CalibrateZero,ReloadZero,FactoryZero,ShowFps,BatteryDisplay,BufferSize,CalibrateHardwareZero,
    GainChannel,GainRange,GainReference,GainReferenceStep,GainProbe,GainSpan,GainInForce,CalibrateGain,FactoryGain,
    InterleaveOffset,InterleaveInForce,InterleaveSaved,SaveInterleave,CancelInterleave,ResetInterleave,
    GeneratorStartup, GeneratorLayout, PanDirection, HoldTime, UsbController, Interleave, CalibrateInterleave, CalibrateInterleaveMatch, ClockIndicator
};
struct SettingsEntry {
    const char *name;
    SettingsAction action = SettingsAction::None;
    SettingsPage child = SettingsPage::Count;
};
struct SettingsPageDefinition {
    const char *title, *path;
    SettingsPage parent;
    const SettingsEntry *entries;
    unsigned count;
};
// The Settings pages and where the user is in them; SettingsProperties shows a page's rows.
class SettingsMenu {
public:
    void open();
    void select(unsigned row) { if (row < count()) selected_[unsigned(page_)] = row; }
    bool back(); // False at the root: return to the instrument.
    // Opens the row's page (if it has one) and returns its action (None for a plain page).
    SettingsAction enter(unsigned row);
    const char *title() const { return definition().title; }
    const char *path() const { return definition().path; }
    unsigned count() const { return definition().count; }
    const SettingsEntry &entry(unsigned row) const;
    const SettingsEntry &current() const { return entry(selected()); }
    unsigned selected() const { return selected_[unsigned(page_)]; }
    SettingsPage page() const { return page_; }
    bool information() const { return page_ == SettingsPage::About || page_ == SettingsPage::Storage; }

private:
    static const SettingsPageDefinition &definition(SettingsPage page);
    const SettingsPageDefinition &definition() const { return definition(page_); }
    SettingsPage page_ = SettingsPage::Root;
    unsigned selected_[unsigned(SettingsPage::Count)]{};
};
} // namespace openosc::ui
