#pragma once
#include "openosc/settings/preferences.hpp"
#include "openosc/settings/tooltips.hpp"
#include "openosc/ui/components/profile_glyph.hpp"
#include "openosc/ui/components/bode_view.hpp"
#include "openosc/settings/scope_appearance.hpp"
#include "openosc/ui/components/scope_styles.hpp"
#include "openosc/ui/scope/controls.hpp"
#include "openosc/ui/screens/generator_screen.hpp"
#include "openosc/ui/screens/meter_screen.hpp"
#include "openosc/ui/screens/scope_screen.hpp"
#include "openosc/ui/editors/frequency_editor.hpp"
#include "openosc/ui/editors/property_list.hpp"
#include "openosc/ui/settings/view.hpp"
#include "openosc/ui/components/choice_popup.hpp"
#include "openosc/ui/components/battery_indicator.hpp"
#include "openosc/ui/components/record_overview.hpp"
#include "openosc/ui/images/browser.hpp"
#include "openosc/ui/components/session_trend.hpp"
#include "openosc/ui/components/menu_content.hpp"
#include "openosc/ui/components/power_hold_overlay.hpp"

namespace openosc {
class ApplicationView {
public:
    ApplicationView() : scope_(host_), meter_(host_), generator_(host_) {}
    void build(const Theme &theme);
    void measurement(const ApplicationState &state);
    void capture(const scope::FrameView &frame, bool held, unsigned channel, DataStatus health);
    void captureStatus(DataStatus status, bool held) { scope_.status(status, held); refreshIndicators(); }
    // source: a channel is shown to trigger on; none (the DMM trace alone): "AUTO", no channel and no level.
    void scopeTrigger(const scope::TriggerSettings &trigger, const scope::ChannelMetadata &channel, bool source);
    void scopeCounters(const scope::CounterSnapshot &snapshot) { scope_.counters(snapshot); }
    bool scopeReadouts(const scope::ReadoutResult &result, const ui::ReadoutState &state) { return scope_.readouts(result, state); }
    bool scopeReadoutsPainted() const { return scope_.readoutsPainted(); }
    void scopeFooterPage() { scope_.nextFooterPage(); }
    const ui::ScopeFooter &scopeFooter() const { return host_.scopeFooter(); } // Its pages and basis indicator.
    void scopeAutoSetup(bool active) { scope_.autoSetup(active); refreshIndicators(); }
    void scopeCalibrating(bool active) { scope_.calibrating(active); refreshIndicators(); }
    void scopeTriggerState(scope::TriggerMode mode, bool waiting) { scope_.triggerState(mode, waiting); refreshIndicators(); }
    // Plan §4.4: a menu or panel over the Scope (the capture may rest), and whether the trace is
    // drawn (not under a menu, not Decoder only: projection, readouts and prepareColumns need it).
    bool overlayActive() const { return host_.overlayActive(); }
    bool traceDrawn() const { return host_.traceDrawn(); }
    // The protocol decoder's view (ScreenHost::decoder), then what it shows: the table (with its
    // info row) and, under the trace, the result of the record the trace shows (DecodeJob::shows).
    void decoder(bool shown, decoding::View view) { host_.decoder(shown, view); } // The same state: nothing.
    bool decoderShown() const { return host_.decoderShown(); }
    void decoderResults(const ui::DecodeTable::Content &table, const char *info, const decoding::ResultSet *traced);
    const ui::DecodeTable &decodeTable() const { return host_.decodeTable(); }
    const ui::DecodeStrip &decodeStrip() const { return host_.decodeStrip(); }
    void scopeSettings(const scope::FrameMetadata &metadata, unsigned channel) { scope_.settings(metadata, channel); }
    void scopeContext(unsigned channel, plot::Focus focus, bool panReverse = false) { scope_.context(channel, focus, panReverse); }
    void scopeMeter(const ui::ScopeScreen::Meter &meter) { scope_.meter(meter); }
    void scopeMeterReadouts(const plot::MeterReadouts &readouts) { scope_.meterReadouts(readouts); }
    // The generator note on the plot; captureHalved: DataAccess::generatorLoadsBus().
    void scopeGenerator(const generator::Snapshot &output, bool captureHalved) { scope_.generator(output, captureHalved); }
    void focusParameter(unsigned index) { host_.parameters().select(index); }
    // Block orders (CONTRACT.md §4): only y moves, nothing is rebuilt (record, HOLD, zoom, leases,
    // statistics stay); the same order is a no-op; an invalid order is ignored. Applied at once when
    // that screen shows, else when it is next activated.
    void scopeArrangement(const ScopeArrangement &order) { host_.scopeArrangement(order); }
    void generatorArrangement(const GeneratorArrangement &order) { host_.generatorArrangement(order); }
    const GeneratorArrangement &generatorArrangement() const { return host_.generatorArrangement(); }
    const ui::BlockSpan &generatorSpan(GeneratorBlock block) const { return host_.generatorSpan(block); } // Tests.
    void meterArrangement(const DigitalArrangement &digital, const AnalogArrangement &analog) {
        meter_.arrangement(digital, analog, mode_ == MeasurementMode::Multimeter && !host_.overlayActive());
    }
    const ScopeArrangement &scopeArrangement() const { return host_.scopeArrangement(); }
    const DigitalArrangement &digitalArrangement() const { return meter_.digitalArrangement(); }
    const AnalogArrangement &analogArrangement() const { return meter_.analogArrangement(); }
    const ui::ParameterBar &softKeys() { return host_.parameters(); } // Tests.
    // The Scope's colours (CONTRACT.md §4, ScopeStyles): its effective theme from the base one, applied
    // without rebuilding anything (record, HOLD, zoom, readouts stay). The same value is a no-op.
    void scopeAppearance(const ScopeAppearance &appearance);
    const ScopeAppearance &scopeAppearance() const { return scopeAppearance_; }
    void multimeter(const dmm::Result &result, bool held, DataStatus status,
                    const dmm::Statistics *statistics = nullptr) { meter_.update(result, held, status, {statistics}); }
    void multimeter(const dmm::Result &result, bool held, DataStatus status, const ui::MeterContext &context) {
        meter_.update(result, held, status, context);
    }
    void generator(const generator::Snapshot &snapshot) { generator_.update(snapshot); }
    void multimeterStatus(const dmm::Result &result, bool held, DataStatus status, bool relative = false, const char *hold = nullptr) {
        meter_.status(result, held, status, relative, hold);
    }
    // Every 8 ms or so while the DMM shows: the bar, and the analog needle's motion.
    void multimeterBar(const dmm::Result &result, DataStatus status, const dmm::RelativeReading *relative, uint32_t now = lv_tick_get()) {
        meter_.fastBar(result,status,relative,now);
    }
    void generatorStatus(const generator::Snapshot &snapshot) { generator_.status(snapshot); }
    // What F1-F4 do now (ui::SoftKeys): a context over the instrument screen's cards, or (nullptr)
    // the screen's own cards again, as the screen last set them.
    void softKeys(const ui::SoftKeys *keys) { if (keys) host_.parameters().context(*keys, menuKeys()); else host_.parameters().release(); }
    const ui::SoftKeys &generatorKeys(const ui::GeneratorKeys &keys) { return generator_.keys(keys); }
    void generatorActive(bool enabled);
    void usbConnected(bool connected);
    void battery(const power::BatteryState &state) { battery_.update(state); }
    void batteryDisplay(uint32_t mode);
    void fps(bool visible, uint32_t value);
    void counter(bool visible, const char *text); // The top bar's counter: FPS or the sample rate.
    // The run profile glyph (GitHub #47): `mode` is ClockControl::activeMode(), RunModeCount none. Enabled
    // in the Scope: left of the battery (StatusBarLayout::clock), the glyph unless no profile runs;
    // LVGL is touched only when the letter, ink, slot or visibility changes.
    void clockProfile(bool enabled, uint32_t mode);
    lv_obj_t *clockGlyph() const { return clock_; } // Tests: its place and visibility.
    WaveformWidget &waveform() { return host_.waveform(); }
    const WaveformWidget &waveform() const { return host_.waveform(); }
    const ui::MeterSummary &meterSummary() { return host_.meterSummary(); }
    const ui::FunctionIcon &functionIcon() { return host_.functionIcon(); }
    const ui::MeterCards &meterCards() { return host_.meterCards(); }
    const ui::AnalogDial &dial() { return host_.dial(); }
    const ui::MeterLook &meterLook() const { return meter_.look(); } // The DMM look in force (tests).
    const lv_area_t &meterReadingBox() const { return meter_.readingBox(); } // The reading row's box (tests).
    const ui::RecordOverview &recordOverview() const { return overview_; }
    lv_obj_t *element(ElementId id) { return host_.elements().get(id); }
    void properties(const ui::PropertyList &properties, unsigned selected); // Browsing on `selected`.
    void properties(const ui::PropertyList &properties, const ui::PropertyNavigator &navigator);
    void frequencyEditor(const ui::FrequencyEditor &editor);
    void settings(const ui::SettingsMenu &menu, const ui::SettingsProperties &rows, const ui::PropertyNavigator &navigator,
                  const char *persistence);
    void information(const ui::SettingsMenu &menu, const DeviceInfo &info);
    void images(const ui::ImageList &images);
    void imagePreview(const char *name,const uint16_t *pixels, bool fullSize = false, const char *keys = "");
    // The saved DMM sessions (GitHub #144): their list, and one's trend in the menu box, titled with
    // its file, drawn by `view` (the caller's, outside this object). Built once (rebuild, or after
    // another menu) and then only redrawn; sessionsClosed() deletes its object before `view` goes.
    void sessions(const ui::ImageList &list);
    void awgFiles(const ui::ImageList &list); // GEN > Waveform > AWG file (docs/AWG.md).
    void captures(const ui::ImageList &list); // Settings > Files & USB > Waveforms (GitHub #186).
    void sessionTrend(const char *name, ui::SessionTrendView &view, const ui::SessionTrendData &data, bool rebuild = false);
    void sessionsClosed();
    const ui::SessionTrendView *sessionTrendView() const { return sessionTrendLive_ ? sessionTrend_ : nullptr; } // Tests.
    // Notice: which notice is on show, for the screenshot's own (dismissed before the next capture).
    enum class Notice : uint8_t { None, General, ScreenshotSaved };
    // A transient notice of its kind (GitHub #108): nothing at all when Settings hides that kind.
    void notify(Tooltip kind, const char *text, uint32_t now, Notice notice = Notice::General);
    void dismissNotice(Notice kind);
    // The kinds Settings > Display > Tooltips hides (Tooltips): from now on, and a notice of a kind
    // just hidden goes at once.
    void tooltips(uint8_t hidden);
    // Settings > Display > Menu contrast (GitHub #44): every menu drawn from now on (the caller
    // redraws one on show), with the keys' cards under it; instrument screens keep their colours.
    void menuContrast(MenuContrast contrast) { menuContrast_ = contrast; }
    MenuContrast menuContrast() const { return menuContrast_; }
    const ui::MenuPalette &menuPalette() const { return menuPalette_; } // Of the menu drawn last (tests).
    // Settings > Display > Swap CH1/CH2 (plot::ChannelView): the Scope's plot, footer, cards and top
    // bar trigger tag follow at once, a held record included; nothing is captured or recomputed.
    void channelView(const plot::ChannelView &view);
    const plot::ChannelView &channelView() const { return host_.channelView(); }
    lv_obj_t *menu() const { return menu_; } // Tests: the menu's box.
    const char *toast() const { return toastShowing() ? lv_label_get_text(toast_) : ""; } // On show now.
    bool toastShowing() const { return notice_ != Notice::None; } // A notice is on show (store(): none over another).
    // DISPlay:TEXT: a message for the person at the device, in a box over every screen (a notice over
    // it) until a key takes it (dismissMessage: true when one was on show), its CLEar or the next one;
    // "" takes it away. message(): the text on show, "" for none.
    void message(const char *text);
    const char *message() const { return messageShowing() ? lv_label_get_text(message_) : ""; }
    bool dismissMessage();
    // The frequency response plot (GitHub #72), hidden until shown. Shown as a menu is: the
    // host's overlay (no Scope elements drawn under it, the keys' bar at the bottom), the menu box hidden.
    ui::BodeView &bode() { return bode_; }
    void bodePlot(const ui::BodeViewData &data);
    void powerHold(int percent, const char *title) { powerHold_.show(percent, title); } // PowerHold::percent; -1 hides.
    void uptime(uint64_t elapsedUs);
    void service(uint32_t now);

private:
    static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y) {
        return ui::Elements::label(parent, text, x, y);
    }
    static const lv_font_t *font(unsigned size) { return ui::Elements::font(size); }
    void menuFrame(const char *title, bool instrument = false, bool retained = false, bool showUptime = false);
    static constexpr int MenuTop = ui::ScreenLayout::MenuTop, MenuHeight = ui::ScreenLayout::MenuHeight; // Above the cards.
    void choice(const ui::PropertyList &list, const ui::PropertyNavigator &navigator, int top); // The open list, if any.
    // The menu's palette for the keys' cards: High contrast with a menu on show, else none (the
    // cards keep the screen's colours, as under the decoder, Bode or Remote).
    const ui::MenuPalette *menuKeys() const;
    ui::ChoicePopup choicePopup_;
    ui::GeneratorPreview draftPreview_;
    lv_obj_t *draftPreviewObject_ = nullptr;
    ui::MenuContent menuContent_;
    bool menuRetained_ = false;
    lv_obj_t *menuUptime_ = nullptr;
    uint32_t uptimeSeconds_ = 0;
    const Theme *theme_ = nullptr;
    ScopeAppearance scopeAppearance_;
    ui::ScopeLook scopeLook_; // ScopeStyles::look(*theme_, scopeAppearance_).
    Theme scopeTheme_;        // ScopeStyles::effective(*theme_, scopeLook_).
    void applyScopeTheme(bool apply);
    // The theme the screen in front draws with: the Scope's appearance on the Scope (its menus,
    // popups and status shell too), the base theme on the DMM and the generator.
    const Theme &shown() const { return mode_ == MeasurementMode::Oscilloscope ? scopeTheme_ : *theme_; }
    void paintShell(); // The status shell, menu frame and toast in shown()'s colours.
    ui::ScreenHost host_;
    ui::BatteryIndicator battery_;
    ui::ImageBrowserView imageBrowser_;
    ui::SessionTrendView *sessionTrend_ = nullptr;
    bool sessionTrendLive_ = false; // Its object is in the menu box now (any other menu cleans it away).
    ui::ScopeScreen scope_;
    ui::MeterScreen meter_;
    ui::GeneratorScreen generator_;
    bool messageShowing() const { return message_ && !lv_obj_is_hidden(message_); }
    lv_obj_t *message_ = nullptr;
    lv_obj_t *menu_ = nullptr, *toast_ = nullptr, *generatorIndicator_ = nullptr, *usbIndicator_ = nullptr, *fps_ = nullptr, *trigger_ = nullptr, *edge_ = nullptr, *channelTag_ = nullptr;
    static constexpr unsigned NoTriggerChannel = 2;
    unsigned channelTagChannel_ = NoTriggerChannel;
    bool triggerSource_ = true; // scopeTrigger's source: the channel tag and the edge are shown.
    uint32_t channelTagColor_ = 0, channelTagInk_ = 0;
    static void drawChannelTag(lv_event_t *event);
    void refreshTriggerColors(unsigned channel);
    ui::RecordOverview overview_;
    ui::PowerHoldOverlay powerHold_;
    void refreshIndicators();
    void placeFps(bool force = false);
    void placeClock();
    void paintClock();
    static void drawClock(lv_event_t *event);
    int statusRight_ = -1; // The pill edge the indicators were last laid out for.
    bool fpsWanted_ = false, fpsVisible_ = false;
    lv_obj_t *clock_ = nullptr;
#ifdef OPENOSC_APPLICATION
    ui::BodeView &bode_ = ui::bodeView(); // ITCM: outside main()'s stack frame.
#else
    ui::BodeView bode_; // The native UI creates several independent views.
#endif
    bool bodeHeader_ = false;
    bool clockEnabled_ = false;
    // Where the glyph and the right side are: 0 the usual places (Off, outside the Scope), 1 beside the
    // battery icon, 2 beside a text battery with USB/GEN/overview closed up (StatusBarLayout::clock).
    uint8_t clockLayout_ = 0;
    uint32_t clockMode_ = RunModeCount, clockInk_ = 0; // The letter and the ink drawn.
    uint32_t toastUntil_ = 0;
    bool generatorOn_ = false;
    Notice notice_ = Notice::None;
    Tooltip noticeKind_ = Tooltip::Critical; // The kind of the notice on show.
    uint8_t tooltipsHidden_ = 0;
    bool usbConnected_ = false;
    MeasurementMode mode_ = MeasurementMode::Oscilloscope;
    MenuContrast menuContrast_ = MenuContrast::Theme;
    ui::MenuPalette menuPalette_; // menuFrame's: every row, list and text of the menu on show.
};
} // namespace openosc
