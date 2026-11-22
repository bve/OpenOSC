#include "openosc/ui/components/parameter_bar.hpp"
#include "openosc/settings/color_value.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace openosc::ui {
namespace {
// LVGL invalidates on every style write, equal or not: only a colour that differs is written, so a
// context or selection that did not change draws nothing.
void textColor(lv_obj_t *object, uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_text_color(object, LV_PART_MAIN), lv_color_hex(color)))
        lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
}
void fillColor(lv_obj_t *object, uint32_t color) {
    if (!lv_color_eq(lv_obj_get_style_bg_color(object, LV_PART_MAIN), lv_color_hex(color)))
        lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
}
void frame(lv_obj_t *object, int width) {
    if (lv_obj_get_style_border_width(object, LV_PART_MAIN) != width) lv_obj_set_style_border_width(object, width, 0);
}
void underline(lv_obj_t *object, bool on) {
    const auto decor = on ? LV_TEXT_DECOR_UNDERLINE : LV_TEXT_DECOR_NONE;
    if (lv_obj_get_style_text_decor(object, LV_PART_MAIN) != decor) lv_obj_set_style_text_decor(object, decor, 0);
}
float luminance(uint32_t rgb) {
    const float r = float(rgb >> 16 & 0xff) / 255, g = float(rgb >> 8 & 0xff) / 255, b = float(rgb & 0xff) / 255;
    return .2126f * r * r + .7152f * g * g + .0722f * b * b; // sRGB decoded as gamma 2: no powf for a guard.
}
} // namespace
float ParameterBar::contrast(uint32_t a, uint32_t b) {
    const float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + .05f) / (std::min(la, lb) + .05f);
}
uint32_t ParameterBar::ink(uint32_t fill) {
    return contrast(ColorContrast::Dark, fill) >= contrast(ColorContrast::Light, fill) ? ColorContrast::Dark : ColorContrast::Light;
}
uint32_t ParameterBar::readable(uint32_t color, uint32_t fill) {
    return contrast(color, fill) >= MinimumContrast ? color : ink(fill);
}
uint32_t ParameterBar::faded(uint32_t ink, uint32_t fill) {
    return lv_color_to_u32(lv_color_mix(lv_color_hex(ink), lv_color_hex(fill), 150)) & 0xffffff; // ~60 % ink.
}
lv_obj_t *ParameterBar::create(lv_obj_t *parent) {
    root_ = lv_obj_create(parent);
    lv_obj_remove_style_all(root_);
    lv_obj_set_scrollable(root_, false);
    selected_ = screenSelected_ = Count;
    context_ = false;
    for (unsigned i = 0; i < Count; ++i) { screen_[i] = {}; live_[i] = true; for (auto *&child : hidden_[i]) child = nullptr; }
    for (unsigned i = 0; i < Count; ++i) {
        cards_[i] = lv_obj_create(root_);
        lv_obj_remove_style_all(cards_[i]);
        lv_obj_set_scrollable(cards_[i], false);
        lv_obj_set_style_bg_opa(cards_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cards_[i], 0, 0);
        lv_obj_set_style_radius(cards_[i], Radius, 0);
        titles_[i] = Elements::label(cards_[i], "");
        values_[i] = Elements::label(cards_[i], "");
        lv_obj_set_style_text_font(titles_[i], Elements::font(12), 0);
        lv_obj_set_style_text_font(values_[i], Elements::font(12), 0);
    }
    for (auto &t : tints_) t = Tint::Highlight;
    bottom_ = false;
    // On the bottom edge the cards reach Radius past the bar, so their lower corners fall outside
    // the screen (the host's 320x240 clips them): square at the edge, round at the top.
    lv_obj_set_overflow_visible(root_, true);
    lv_obj_add_event_cb(root_, resized, LV_EVENT_SIZE_CHANGED, this);
    lv_obj_set_size(root_, 304, 32);
    return root_;
}
void ParameterBar::arrange() {
    const int width = lv_obj_get_width(root_), height = lv_obj_get_height(root_);
    const int gap = width >= 80 ? 4 : 0;
    const int available = std::max<int>(4, width - 3 * gap);
    for (unsigned i = 0; i < Count; ++i) { // Equal zones: one per key under the screen.
        const int x = int(i) * available / int(Count) + int(i) * gap;
        const int w = (int(i) + 1) * available / int(Count) - int(i) * available / int(Count);
        widths_[i] = w;
        lv_obj_set_pos(cards_[i], x, 0);
        lv_obj_set_size(cards_[i], w, height + (bottom_ ? Radius : 0));
        lv_obj_set_pos(titles_[i], 4, 1);
        lv_obj_set_size(titles_[i], std::max(1, w - 8), 14);
        const int line = lv_font_get_line_height(lv_obj_get_style_text_font(values_[i], LV_PART_MAIN));
        const int top = height >= line + 16 ? 16 : 1;
        lv_obj_set_hidden(titles_[i], top == 1);
        lv_obj_set_pos(values_[i], 4, top);
        lv_obj_set_size(values_[i], std::max(1, w - 8), std::max(1, std::min(line, height - top)));
        fit(i);
    }
}
void ParameterBar::fit(unsigned i) {
    if (customFont_) return; // The DMM's Card text font is the user's choice.
    for (auto *label : {titles_[i], values_[i]}) {
        const auto *current = lv_obj_get_style_text_font(label, LV_PART_MAIN);
        if (current != Elements::font(12) && current != Elements::font(11)) continue; // Another theme size: kept.
        const bool condensed = label == values_[i] && !context_ && screen_[i].condensed;
        const auto *font = !condensed && Elements::textWidth(lv_label_get_text(label), 12) <= widths_[i] - 8 ? Elements::font(12) : Elements::font(11);
        if (current != font) lv_obj_set_style_text_font(label, font, 0);
    }
}
void ParameterBar::resized(lv_event_t *event) {
    static_cast<ParameterBar *>(lv_event_get_user_data(event))->arrange();
}
void ParameterBar::palette(const Theme &theme, const ElementStyle &style) {
    background_ = theme.panel;
    border_ = theme.grid;
    foreground_ = theme.foreground;
    highlight_ = style.color;
    muted_ = theme.muted;
    channels_[0] = theme.channel1; channels_[1] = theme.channel2;
    neutral_ = theme.foreground;
    selectedText_ = customFont_ = false;
    if (screenSelected_ >= Count) screenSelected_ = 0;
    if (!context_) selected_ = screenSelected_;
    for (unsigned i = 0; i < Count; ++i) {
        lv_obj_set_style_border_color(cards_[i], lv_color_hex(border_), 0); // An inert key's frame.
        lv_obj_set_style_text_font(values_[i], Elements::font(style.font), 0);
        lv_obj_set_style_text_font(titles_[i], Elements::font(12), 0);
    }
    paint();
    arrange();
}
void ParameterBar::text(const lv_font_t *font, uint32_t color, bool selected) {
    foreground_ = color;
    selectedText_ = selected;
    customFont_ = true;
    for (unsigned i = 0; i < Count; ++i) {
        if (lv_obj_get_style_text_font(values_[i], LV_PART_MAIN) != font) lv_obj_set_style_text_font(values_[i], font, 0);
        if (lv_obj_get_style_text_font(titles_[i], LV_PART_MAIN) != font) lv_obj_set_style_text_font(titles_[i], font, 0);
    }
    paint();
    arrange();
}
uint32_t ParameterBar::accent(unsigned index) const {
    switch (context_ ? Tint::Highlight : tints_[index]) {
    case Tint::Channel1: return channels_[0];
    case Tint::Channel2: return channels_[1];
    case Tint::Neutral: return neutral_;
    default: return highlight_;
    }
}
// The layer on show. A live card is filled: the selected one with its accent, its texts in the ink
// that contrasts with it (an explicit Card text colour where it stays readable there), a marked
// title underlined; the others with the grid colour, the value in the text colour, the title muted
// or, marked, in the highlight. An inert key: the panel inside the grid frame, its texts muted.
// Only what changed is written: an unchanged state redraws nothing.
void ParameterBar::paint() {
    for (unsigned i = 0; i < Count; ++i) {
        const bool live = live_[i], selected = accented(i), marked = !context_ && screen_[i].marked;
        const uint32_t fill = !live ? background_ : selected ? accent(i) : border_;
        fills_[i] = fill;
        fillColor(cards_[i], fill);
        frame(cards_[i], live ? 0 : 1);
        const uint32_t on = ink(fill);
        textColor(values_[i], !live ? readable(muted_, fill)
                        : selected ? (selectedText_ && contrast(foreground_, fill) >= MinimumContrast ? foreground_ : on)
                        : readable(foreground_, fill));
        textColor(titles_[i], !live ? readable(muted_, fill) : selected ? on : readable(marked ? highlight_ : muted_, fill));
        underline(titles_[i], selected && marked);
    }
}
void ParameterBar::tint(unsigned index, Tint tint) {
    if (index >= Count || tints_[index] == tint) return;
    tints_[index] = tint;
    if (!context_ && index == selected_) paint();
}
void ParameterBar::untinted() {
    for (unsigned i = 0; i < Count; ++i) tint(i, Tint::Highlight);
}
void ParameterBar::edge(bool bottom) {
    if (bottom == bottom_) return;
    bottom_ = bottom;
    if (root_) arrange();
}
void ParameterBar::select(unsigned index) {
    screenSelected_ = index;
    if (context_ || selected_ == index) return;
    selected_ = index;
    paint();
}
void ParameterBar::show(unsigned index, const char *title, const char *value) {
    if (std::strcmp(lv_label_get_text(titles_[index]), title)) { lv_label_set_text(titles_[index], title); fit(index); }
    if (std::strcmp(lv_label_get_text(values_[index]), value)) { lv_label_set_text(values_[index], value); fit(index); }
}
void ParameterBar::item(unsigned index, const char *title, const char *value, bool marked, bool condensed) {
    if (index >= Count) return;
    auto &card = screen_[index];
    std::snprintf(card.title, sizeof card.title, "%s", title);
    std::snprintf(card.value, sizeof card.value, "%s", value);
    const bool remark = card.marked != marked, refit = card.condensed != condensed;
    card.marked = marked; card.condensed = condensed;
    if (context_) return; // Kept for release().
    show(index, card.title, card.value);
    if (refit) fit(index);
    if (remark) paint();
}
void ParameterBar::decorations(bool visible) {
    for (unsigned i = 0; i < Count; ++i) {
        if (!visible) {
            unsigned n = 0;
            for (uint32_t c = 0; c < lv_obj_get_child_count(cards_[i]) && n < Decorations; ++c) {
                auto *child = lv_obj_get_child(cards_[i], int32_t(c));
                if (child == titles_[i] || child == values_[i] || lv_obj_is_hidden(child)) continue;
                lv_obj_set_hidden(child, true);
                hidden_[i][n++] = child;
            }
        } else {
            for (auto *&child : hidden_[i]) { if (child) lv_obj_set_hidden(child, false); child = nullptr; }
        }
    }
}
void ParameterBar::decorate(lv_obj_t *child, bool visible) {
    if (!child) return;
    for (unsigned i = 0; i < Count; ++i) {
        if (lv_obj_get_parent(child) != cards_[i]) continue;
        unsigned free = Decorations;
        for (unsigned n = 0; n < Decorations; ++n) {
            if (hidden_[i][n] == child) { if (!visible) hidden_[i][n] = nullptr; lv_obj_set_hidden(child, true); return; }
            if (!hidden_[i][n] && free == Decorations) free = n;
        }
        if (context_ && visible && free < Decorations) { hidden_[i][free] = child; lv_obj_set_hidden(child, true); return; } // At release().
    }
    if (lv_obj_is_hidden(child) == visible) lv_obj_set_hidden(child, !visible);
}
void ParameterBar::context(const SoftKeys &keys) {
    if (!context_) decorations(false);
    context_ = true;
    for (unsigned i = 0; i < Count; ++i) {
        live_[i] = keys.keys[i].live;
        show(i, keys.keys[i].title, keys.keys[i].value);
        if (screen_[i].condensed) fit(i);
    }
    selected_ = keys.selected < Count && keys.keys[keys.selected].live ? keys.selected : Count;
    paint();
}
void ParameterBar::release() {
    if (!context_) return;
    context_ = false;
    for (unsigned i = 0; i < Count; ++i) {
        live_[i] = true;
        show(i, screen_[i].title, screen_[i].value);
        if (screen_[i].condensed) fit(i); // The same text as the context's keeps its face otherwise.
    }
    decorations(true);
    selected_ = screenSelected_;
    paint();
}
} // namespace openosc::ui
