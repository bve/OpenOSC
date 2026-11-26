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
bool PropertyNavigator::backPressed(uint32_t keys) const {
    return (keys & Keypad::F4) || (!leftStep_ && (keys & Keypad::Left & ~repeated_));
}
// Forward wraps. Backward stops at the first option and leaves a legacy value alone.
PropertyNavigator::Outcome PropertyNavigator::next(PropertyList &list, bool previous) const {
    const unsigned current = list.option(row_);
    if (list.kind(row_) == PropertyKind::Toggle) {
        if (previous && !current) return Outcome::None;
        return list.choose(row_, current ? 0 : 1) ? Outcome::Changed : Outcome::Refused;
    }
    const unsigned options = list.options(row_);
    if (!options || (previous && (!current || current >= options))) return Outcome::None;
    const unsigned next = previous ? current - 1 : current < options ? (current + 1) % options : 0;
    if (next == current) return Outcome::None;
    return list.choose(row_, next) ? Outcome::Changed : Outcome::Refused;
}
PropertyNavigator::Outcome PropertyNavigator::keys(uint32_t keys, PropertyList &list) {
    const bool back = backPressed(keys);
    const uint32_t pressed = keys & ~repeated_;
    repeated_ = 0; // This call's only.
    const unsigned count = list.count();
    if (!keys) return Outcome::None;
    if (!count) { mode_ = Mode::Browse; return Outcome::None; } // Emptied: an open list or edit closes, no row to read.
    const bool gone = row_ >= count;
    if (gone) row_ = count - 1;
    const auto kind = list.kind(row_);
    // An open list or edit whose row is gone (or now another kind): the key only closes it.
    if ((mode_ == Mode::Choice && (gone || kind != PropertyKind::Choice)) || (mode_ == Mode::Number && (gone || kind != PropertyKind::Number))) {
        mode_ = Mode::Browse; return Outcome::None;
    }
    switch (mode_) {
    case Mode::Choice: {
        const unsigned options = list.options(row_);
        if (!options) { mode_ = Mode::Browse; return Outcome::None; } // The list emptied while open.
        if (back) { mode_ = Mode::Browse; return Outcome::None; }
        if (draft_ >= options) { draft_ = 0; return Outcome::None; } // It shrank: the key shows its first option, takes none.
        if (keys & Keypad::Menu) { // Wins over a move pressed with it.
            mode_ = Mode::Browse;
            if (draft_ == list.option(row_)) return Outcome::None;
            return list.choose(row_, draft_) ? Outcome::Changed : Outcome::Refused;
        }
        if (keys & Keypad::Up) draft_ = (draft_ + options - 1) % options;
        else if (keys & Keypad::Down) draft_ = (draft_ + 1) % options;
        else if (leftStep_ && (pressed & Keypad::Left) && draft_) --draft_;
        else if (leftStep_ && (pressed & Keypad::Right) && draft_ + 1 < options) ++draft_;
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
    if (back) return Outcome::Back;
    if (keys & Keypad::Up) { row_ = (row_ + count - 1) % count; return Outcome::None; }
    if (keys & Keypad::Down) { row_ = (row_ + 1) % count; return Outcome::None; }
    if ((keys & Keypad::Right) && kind == PropertyKind::Page) return Outcome::Open;
    if (kind == PropertyKind::Choice || kind == PropertyKind::Toggle) {
        if (keys & Keypad::Right) return pressed & Keypad::Right ? next(list) : Outcome::None;
        if (leftStep_ && (pressed & Keypad::Left) && !(keys & Keypad::Menu)) return next(list, true);
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
