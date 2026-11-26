#pragma once
#include "openosc/measurement/dmm/function.hpp"
#include "openosc/measurement/dmm/temperature_axis.hpp"
#include "openosc/ui/screens/instrument_screen.hpp"
#include "openosc/ui/screens/meter_context.hpp"
#include "openosc/ui/components/meter_style.hpp"
#include "openosc/ui/screens/screen_layout.hpp"

namespace openosc::ui {
// The DMM screen: places the function sign, the reading or the analog dial, the summary/trend and
// applies the DMM palette and reading face. The components draw; this only arranges and feeds them.
class MeterScreen final : public InstrumentScreen {
public:
    static constexpr unsigned AnalogDisplay = 3; // MeterPreferences::display.
    static constexpr int UnitGap = 8; // Between the digits and their unit (digital and analog).
    // The theme's default DMM body began at y 55 under its top cards: its sign row's y there is the
    // origin its element positions are taken relative to inside the reading block.
    static constexpr int ThemeBodyTop = 55;
    using InstrumentScreen::InstrumentScreen;
    void activate(const ApplicationState &state) override;
    void update(const dmm::Result &result, bool held, DataStatus status, const MeterContext &context);
    // The state pill. held: the reading shown is frozen (a HOLD or a hold mode's snapshot); `mode`: the
    // hold mode's word in place of "HOLD" (or, armed over live readings, before the state: "WAIT").
    void status(const dmm::Result &result, bool held, DataStatus status, bool relative = false, const char *mode = nullptr);
    // The bar and the needle, every ~8 ms: `now` is the monotonic time the needle moves by.
    void fastBar(const dmm::Result &result, DataStatus status, const dmm::RelativeReading *relative, uint32_t now);
    // The block orders of the digital faces and of the analog dial (ScreenLayout): placed at once
    // when `shown` (the DMM is the screen and no menu covers it), else on its next activation; the
    // same orders change nothing; no element is rebuilt.
    void arrangement(const DigitalArrangement &digital, const AnalogArrangement &analog, bool shown);
    const DigitalArrangement &digitalArrangement() const { return digital_; }
    const AnalogArrangement &analogArrangement() const { return analogOrder_; }
    const BlockSpan &digitalSpan(DigitalBlock block) const { return digitalSpans_[unsigned(block)]; }
    const BlockSpan &analogSpan(AnalogBlock block) const { return analogSpans_[unsigned(block)]; }

    const MeterLook &look() const { return look_; } // What is applied (tests).
    const lv_area_t &readingBox() const { return readingBox_; } // The reading row's box (tests).

private:
    void feed(const dmm::Result &result, DataStatus status, const dmm::RelativeReading *relative); // Bar or dial target.
    void reading(const char *text, DataStatus state);
    // The DMM's look (palette, face, overrides): resolved and applied only when it changes.
    void style(unsigned face, unsigned palette, const MeterAppearance &appearance);
    void layout(bool analog);
    void cards(dmm::Function function, bool beeper, bool currentInput, unsigned focus, const char *unit);
    unsigned face_ = 0, palette_ = 0;
    MeterAppearance appearance_;
    MeterLook look_;
    bool analog_ = false, held_ = false, styled_ = false;
    bool statistics_ = true; // The measurements block has its statistics' rows (or the trend).
    bool recording_ = false;
    bool bar_ = true;
    dmm::TemperatureAxis temperatureAxis_;
    DataStatus shownState_ = DataStatus::NoData; // Of the reading shown (its row is placed again on a layout).
    lv_area_t readingBox_{}; // The reading row's box (MeterReading::showRow): the theme's, in its block.
    int digits_ = ScreenLayout::Digits; // Digital: the size the reading block holds (ScreenLayout::digitsFor).
    DigitalArrangement digital_;
    AnalogArrangement analogOrder_;
    BlockSpan digitalSpans_[DigitalArrangement::Size], analogSpans_[AnalogArrangement::Size];
};
} // namespace openosc::ui
