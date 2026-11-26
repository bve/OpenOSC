#pragma once
#include "openosc/ui/components/elements.hpp"
#include "openosc/ui/components/menu_palette.hpp"
#include "openosc/ui/components/soft_keys.hpp"
#include "openosc/ui/components/status_bar_layout.hpp"

namespace openosc::ui {
// The four soft-key zones over F1..F4: equal widths (ScreenLayout::ButtonWidth on its 316 px bar),
// each a title row (the action, or its context) and a value row. A text too wide for IBM Plex 12
// takes the condensed 11 px face, so it stays whole; a marked title (the Scope's Next scales) is
// drawn in the highlight colour. Two layers: the instrument screen's cards (item, select), kept as
// last set, and a context's (context: a panel, menu, confirmation, calibration, Remote) drawn over
// them until release(); the screen's background updates never show through a context.
// Cards (issue #21): rounded as the RUN/HOLD pill (StatusBarLayout::PillRadius), all four corners,
// or only the top two where the bar stands on the screen's bottom edge (edge()). A live card is
// filled, no frame: the grid colour, the selected one its accent (the highlight, or the screen's
// tint for that key); an inert key is an empty frame. Every text keeps MinimumContrast to its fill,
// else takes the dark or light ink that contrasts more (readable()); on an accent fill the texts
// are that ink and a marked title is underlined instead of coloured.
class ParameterBar {
public:
    static constexpr unsigned Count = 4;
    static constexpr int Radius = StatusBarLayout::PillRadius;
    static constexpr float MinimumContrast = 3.0f; // WCAG ratio for UI text and marks.
    // The accent of a screen card when selected: the highlight, a channel's colour (the Scope's F1-F3),
    // the theme's text colour (neutral) or the Scope's DMM trace colour (meterTint). Resolved from the
    // theme at palette(); a context's keys always take the highlight, so channel colours never reach a
    // menu, popup or the generator.
    enum class Tint : uint8_t { Highlight, Channel1, Channel2, Neutral, Meter };
    static uint32_t readable(uint32_t color, uint32_t fill); // `color`, or the contrasting ink when too faint on `fill`.
    static uint32_t ink(uint32_t fill);                      // The dark or light ink with the higher contrast.
    static uint32_t faded(uint32_t ink, uint32_t fill);      // A secondary mark on an accent fill: ink part-way to it.
    static float contrast(uint32_t a, uint32_t b);           // WCAG ratio (gamma 2 approximation), 1..21.
    lv_obj_t *create(lv_obj_t *parent);
    void palette(const Theme &theme, const ElementStyle &style);
    // The cards' text after palette() (the DMM's Card text): its font and colour. `selected`: the
    // selected card's text takes it too (an explicit colour; the frame keeps the highlight); else
    // the selected text stays in the highlight. palette() on the next mode change restores the theme's.
    void text(const lv_font_t *font, uint32_t color, bool selected);
    // `condensed`: the value keeps the condensed 11 px face whatever its text (the Scope's F1 scales,
    // issue #90: no change of face as they change); the screen layer only, a context fits its own.
    void item(unsigned index, const char *title, const char *value, bool marked = false, bool condensed = false);
    void select(unsigned index);
    // A context's keys over the screen's: inert keys muted, never accented; the screen's own drawings
    // on the cards (the DMM's function signs) hidden. release(): the screen's cards as last set.
    // `menu`: the cards in that menu's colours while this context shows (High contrast, GitHub #44:
    // a live card on its selection fill, an inert one on its surface in its frame, the selected one
    // in its accent); null: the screen's colours. The screen's own cards never take it.
    void context(const SoftKeys &keys, const MenuPalette *menu = nullptr);
    void release();
    // The screen's own drawing on a card shown or hidden (the DMM's signs): under a context it stays
    // hidden and appears with release(), whenever the screen asks.
    void decorate(lv_obj_t *child, bool visible);
    void tint(unsigned index, Tint tint); // The screen card's accent (kept until untinted()).
    // Tint::Meter's colour: the Scope appearance's DMM role, which the theme has no field for (before palette()).
    void meterTint(uint32_t color) { meter_ = color; }
    void untinted();                      // Every screen card back to the highlight.
    void edge(bool bottom);               // The bar stands on the screen's bottom edge.
    uint32_t fill(unsigned index) const { return index < Count ? fills_[index] : background_; } // As painted.
    bool accented(unsigned index) const { return index == selected_ && index < Count && live_[index]; }
    bool contextShown() const { return context_; }
    bool live(unsigned index) const { return index < Count && live_[index]; } // Tests: the key acts here.
    lv_obj_t *card(unsigned index) const { return index < Count ? cards_[index] : nullptr; }
    unsigned selected() const { return selected_; } // On show (the context's while one is shown).
    lv_obj_t *title(unsigned index) const { return index < Count ? titles_[index] : nullptr; } // Tests.
    lv_obj_t *value(unsigned index) const { return index < Count ? values_[index] : nullptr; }

private:
    static constexpr unsigned TitleBytes = 24, ValueBytes = 40, Decorations = 4;
    struct Card { char title[TitleBytes] = "", value[ValueBytes] = ""; bool marked = false, condensed = false; };
    static void resized(lv_event_t *event);
    void show(unsigned index, const char *title, const char *value); // Texts on the labels.
    void paint();                         // Borders and colours of the layer on show.
    void decorations(bool visible);       // The screen's own children of the cards.
    void arrange();
    void fit(unsigned index); // The title's and the value's face: 12, or condensed 11 when 12 does not fit (or Card::condensed).
    lv_obj_t *root_ = nullptr;
    lv_obj_t *cards_[Count]{}, *titles_[Count]{}, *values_[Count]{};
    unsigned selected_ = Count, screenSelected_ = Count;
    Card screen_[Count];                  // The screen's cards, as last set (shown when no context is).
    bool live_[Count]{true, true, true, true}, context_ = false;
    lv_obj_t *hidden_[Count][Decorations]{}; // What context() hid, shown again by release().
    bool selectedText_ = false, customFont_ = false; // The selected card's text in foreground_; text() font in force.
    int widths_[Count]{};
    Tint tints_[Count]{};
    bool bottom_ = false;
    uint32_t fills_[Count]{};
    uint32_t background_ = 0, border_ = 0, highlight_ = 0, foreground_ = 0, muted_ = 0;
    uint32_t channels_[2]{}, neutral_ = 0, meter_ = 0;
    uint32_t accent(unsigned index) const; // The selected card's fill.
    bool menuColors_ = false; // The context shows in menu_'s colours.
    MenuPalette menu_;
};
} // namespace openosc::ui
