#include "openosc/ui/editors/property_navigator.hpp"
#include "openosc/platform/input.hpp"

namespace openosc::ui {
void PropertyNavigator::restore(PropertyList &list) const { list.cancelEdit(row_, saved_); }
bool PropertyNavigator::cancel(PropertyList &list) {
    // A row gone or no longer a Number keeps its value: the restore never lands on another row.
    const bool number = mode_ == Mode::Number && row_ < list.count() && list.kind(row_) == PropertyKind::Number;
    if (number) restore(list);
    mode_ = Mode::Browse;
    return number;
}
// Next option (wraps; off the list: the first); back: the previous, none before the first, a Toggle On to Off only.
PropertyNavigator::Outcome PropertyNavigator::next(PropertyList &list, bool back) const {
    const unsigned current = list.option(row_);
    if (list.kind(row_) == PropertyKind::Toggle) {
        if (back && !current) return Outcome::None;
        return list.choose(row_, current ? 0 : 1) ? Outcome::Changed : Outcome::Refused;
    }
    const unsigned options = list.options(row_);
    if (!options) return Outcome::None;
    if (back && current == 0) return Outcome::None;
    if (back && current >= options) return Outcome::None; // An old value off the list: no guess below it.
    const unsigned next = current >= options ? 0 : back ? current - 1 : (current + 1) % options;
    if (next == current) return Outcome::None;
    return list.choose(row_, next) ? Outcome::Changed : Outcome::Refused;
}
PropertyNavigator::Outcome PropertyNavigator::keys(uint32_t keys, PropertyList &list) {
    const uint32_t repeated = repeated_;
    repeated_ = 0; // This call's only.
    const unsigned count = list.count();
    if (!keys) return Outcome::None;
    if (!count) { mode_ = Mode::Browse; return Outcome::None; } // Emptied: an open list or edit closes, no row to read.
    const bool gone = row_ >= count;
    if (gone) row_ = count - 1;
    const auto kind = list.kind(row_);
    const uint32_t backKeys = Keypad::F4 | (leftBack_ && !(repeated & Keypad::Left) ? uint32_t(Keypad::Left) : 0U); // Left on its press only.
    // An open list or edit whose row is gone (or now another kind): the key only closes it.
    if ((mode_ == Mode::Choice && (gone || kind != PropertyKind::Choice)) || (mode_ == Mode::Number && (gone || kind != PropertyKind::Number))) {
        mode_ = Mode::Browse; return Outcome::None;
    }
    switch (mode_) {
    case Mode::Choice: {
        const unsigned options = list.options(row_);
        if (!options) { mode_ = Mode::Browse; return Outcome::None; } // The list emptied while open.
        if (keys & backKeys) { mode_ = Mode::Browse; return Outcome::None; }
        if (draft_ >= options) { draft_ = 0; return Outcome::None; } // It shrank: the key shows its first option, takes none.
        if (keys & Keypad::Menu) { // Wins over a move pressed with it.
            mode_ = Mode::Browse;
            if (draft_ == list.option(row_)) return Outcome::None;
            return list.choose(row_, draft_) ? Outcome::Changed : Outcome::Refused;
        }
        if (keys & Keypad::Up) draft_ = (draft_ + options - 1) % options;
        else if (keys & Keypad::Down) draft_ = (draft_ + 1) % options;
        else if (keys & Keypad::Left) { if (draft_) --draft_; } // As Step: no wrap.
        else if ((keys & Keypad::Right) && !leftBack_ && draft_ + 1 < options) ++draft_; // Undoes Left.
        return Outcome::None;
    }
    case Mode::Number:
        if (keys & Keypad::F4) { restore(list); mode_ = Mode::Browse; return Outcome::Restored; }
        if (keys & Keypad::Menu) { mode_ = Mode::Browse; return list.edited(row_, saved_) ? Outcome::Changed : Outcome::None; }
        if (keys & Keypad::Right) { list.adjust(row_, 1); return Outcome::Stepped; }
        if (keys & Keypad::Up) { list.adjust(row_, list.coarseStep(row_)); return Outcome::Stepped; }
        if (keys & Keypad::Left) { list.adjust(row_, -1); return Outcome::Stepped; }
        if (keys & Keypad::Down) { list.adjust(row_, -list.coarseStep(row_)); return Outcome::Stepped; }
        return Outcome::None;
    case Mode::Browse: break;
    }
    if (keys & backKeys) return Outcome::Back;
    if (keys & Keypad::Up) { row_ = (row_ + count - 1) % count; return Outcome::None; }
    if (keys & Keypad::Down) { row_ = (row_ + 1) % count; return Outcome::None; }
    if ((keys & Keypad::Right) && kind == PropertyKind::Page) return Outcome::Open;
    if ((keys & Keypad::Right) && (kind == PropertyKind::Choice || kind == PropertyKind::Toggle))
        return repeated & Keypad::Right ? Outcome::None : next(list); // One option per press, never on a held key.
    if ((keys & Keypad::Left) && !(keys & Keypad::Menu)) { // Step (MENU, if pressed too, wins).
        if (kind != PropertyKind::Choice && kind != PropertyKind::Toggle) return Outcome::None;
        return repeated & Keypad::Left ? Outcome::None : next(list, true);
    }
    if (!(keys & Keypad::Menu)) return Outcome::None;
    switch (kind) {
    case PropertyKind::Choice:
        if (!list.options(row_)) return Outcome::None;
        // A value off the list (a legacy one) starts the draft on the first option; it stays as it
        // is until an option is taken.
        mode_ = Mode::Choice; draft_ = list.option(row_) < list.options(row_) ? list.option(row_) : 0;
        return Outcome::None;
    case PropertyKind::Toggle: return next(list);
    case PropertyKind::Number: mode_ = Mode::Number; saved_ = list.number(row_); list.beginEdit(row_); return Outcome::None;
    case PropertyKind::Action: return Outcome::Activate;
    case PropertyKind::Page: return Outcome::Open;
    case PropertyKind::Info: return Outcome::None;
    }
    return Outcome::None;
}
} // namespace openosc::ui
