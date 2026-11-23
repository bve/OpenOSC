#include "openosc/ui/settings/menu.hpp"

namespace openosc::ui {
void SettingsMenu::open() {
    page_ = SettingsPage::Root;
    for (auto &selected : selected_) selected = 0;
}
void SettingsMenu::open(SettingsPage page, SettingsAction action) {
    open();
    // Each parent's selection is the row that leads down, so Back returns along the same path.
    for (auto child = page; child != SettingsPage::Root && child < SettingsPage::Count;) {
        const auto parent = definition(child).parent;
        const auto &above = definition(parent);
        for (unsigned i = 0; i < above.count; ++i)
            if (above.entries[i].child == child) selected_[unsigned(parent)] = uint8_t(i);
        child = parent;
    }
    page_ = page < SettingsPage::Count ? page : SettingsPage::Root;
    for (unsigned i = 0; i < count(); ++i)
        if (entry(i).action == action) selected_[unsigned(page_)] = uint8_t(i);
}
bool SettingsMenu::back() {
    if (page_ == SettingsPage::Root) return false;
    page_ = definition().parent;
    return true;
}
SettingsAction SettingsMenu::enter(unsigned row) {
    select(row);
    const auto item = entry(row);
    if (item.child != SettingsPage::Count) page_ = item.child;
    return item.action;
}
const SettingsEntry &SettingsMenu::entry(unsigned row) const {
    static constexpr SettingsEntry empty{"", SettingsAction::None};
    return row < count() ? definition().entries[row] : empty;
}
} // namespace openosc::ui
