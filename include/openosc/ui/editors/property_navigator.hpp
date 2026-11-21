#pragma once
#include "openosc/ui/editors/property_list.hpp"

namespace openosc::ui {
// The keys of every property list, one way (Settings, Scope, DMM and GEN panels):
// Browse: Up/Down rows; MENU opens the row (a Choice list, a Number edit), switches a Toggle,
//         runs an Action or opens a Page; Right opens a Page, and takes the next option of a Choice
//         (the first after the last) or switches a Toggle, once per press (not on its auto-repeat);
//         on a Number, Action or Info row Right does nothing; F4 goes back. Left (leftBack): Back, or
//         Step: the option before (none before the first; a Toggle: On to Off only), else nothing.
// Choice: one move a key event: Up/Down in the list (Left as Step: Left/Right up/down, no wrap); MENU takes
//         it, over any move; F4 (and Left as Back) cancel.
// Number: Left/Right and Up/Down step the value; MENU keeps it; F4 restores it.
// One field at a time is committed: a Choice taken, a Toggle switched, a Number finished with MENU
// (Changed). A Number's steps (Stepped) and its F4 restore (Restored) are the edit in progress: an
// owner that previews live (brightness) applies them, every other owner only redraws its draft.
// The navigator changes the list only through choose(), adjust() and cancelEdit().
class PropertyNavigator {
public:
    enum class Mode : uint8_t { Browse, Choice, Number };
    enum class Outcome : uint8_t {
        None,     // Nothing to do (or only a redraw: the row or the draft moved).
        Changed,  // A field committed: a choice taken, a toggle, a number edit finished with a new value.
        Stepped,  // A number edit's step: the draft value only.
        Restored, // A number edit cancelled (F4): the value it began with is back in the list.
        Refused,  // The list refused the choice or toggle: its notice() says why.
        Back,     // F4 (or Left as Back) in Browse.
        Activate, // MENU on an Action row.
        Open      // MENU or Right on a Page row.
    };
    void reset(unsigned row = 0) { row_ = row; mode_ = Mode::Browse; draft_ = 0; saved_ = 0; }
    Outcome keys(uint32_t keys, PropertyList &list);
    // Of the keys the next keys() call gets, those that are a held arrow's auto-repeat (KeyEvents::repeat);
    // that call alone uses them. Never set: every key is a press.
    void repeats(uint32_t keys) { repeated_ = keys; }
    bool repeating(uint32_t key) const { return repeated_ & key; } // Before keys() takes them.
    // Left: false steps back (Browse: a Choice or Toggle, once per press; Choice: the draft), true is F4.
    void leftBack(bool back) { leftBack_ = back; }
    // Leaves a list or number edit as if F4 were pressed (MODE, Remote, power off while a list is
    // open); true if a number was restored, which its owner then applies.
    bool cancel(PropertyList &list);
    unsigned row() const { return row_; }
    Mode mode() const { return mode_; }
    unsigned draft() const { return draft_; }

private:
    void restore(PropertyList &list) const;
    Outcome next(PropertyList &list, bool back = false) const;
    unsigned row_ = 0, draft_ = 0;
    uint32_t repeated_ = 0;
    bool leftBack_ = true; // As Preferences::menuLeftBack.
    int32_t saved_ = 0;
    Mode mode_ = Mode::Browse;
};
} // namespace openosc::ui
