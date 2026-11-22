#include "openosc/application/controller.hpp"
#include "openosc/ui/editors/gain_match_progress.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/acquisition/ranges.hpp"
#include <new>

namespace openosc {
namespace {
using Step = calibration::ChannelMatch::Step;
using Row = ui::GainMatchProgress::Row;
using Match = calibration::ChannelMatch;
// The single controller's histogram workspace lives in SRAM4 so it leaves the DTCM stack free.
static_assert(sizeof(Match) <= 1856);
alignas(Match) uint8_t storage[sizeof(Match)] __attribute__((section(".settings_workspace"), used));
Match &gainMatch() {
    static bool initialized = false;
    if (!initialized) { new (storage) Match(); initialized = true; }
    return *reinterpret_cast<Match *>(storage);
}
const char *readiness(DataStatus status) {
    switch (status) {
    case DataStatus::Uncalibrated: return "Factory calibration unavailable";
    case DataStatus::IoError: return "Gain JSON unread or disk unavailable";
    case DataStatus::InvalidArgument: return "Both channels on, same V/div, DC";
    default: return "Enable RUN and finish other operations";
    }
}
}
// Scope calibration > Gain > Match to other channel (GitHub #80): the Gain page's Channel is the one
// corrected, at the V/div both run at, on the user's own RUN records (nothing is set up for it).
void ApplicationController::openGainMatch(uint32_t now) {
    if (autoBusy() || state_.mode != MeasurementMode::Oscilloscope) { view_.notify("Select Scope first", now); return; }
    const auto ready = data_.gainMatchReady();
    if (ready != DataStatus::Ok) { view_.notify(readiness(ready), now); return; }
    const unsigned target = calibrationDraft_.channel & 1;
    const auto &setup = data_.scopeMetadata();
    unsigned range = 0;
    scope::VoltageRanges::index(setup.channels[target].millivoltsPerDivision, range);
    if (data_.gainCellBlocked(target, range)) { view_.notify("Factory gain selected: saved cells would return", now); return; }
    gainMatch().begin(target, setup, data_.gainInForce(target, range));
    state_.settingsOpen = false;
    const ui::GainMatchProgress rows(gainMatch());
    openPanel(Panel::GainMatch, rows, rows.defaultRow());
}
void ApplicationController::showGainMatch() {
    const ui::GainMatchProgress rows(gainMatch());
    navigator_.reset(rows.defaultRow());
    view_.properties(rows, navigator_);
}
void ApplicationController::finishGainMatch(uint32_t now, const char *notice, bool returnToSettings) {
    panel_ = Panel::None;
    if (returnToSettings) {
        state_.settingsOpen = true;
        navigator_.reset(settingsMenu_.selected());
        showSettings();
    }
    if (notice) view_.notify(notice, now);
}
void ApplicationController::gainMatchKeys(uint32_t keys, uint32_t now) {
    auto &match = gainMatch();
    ui::GainMatchProgress rows(match);
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto outcome = (keys & Keypad::Mode) ? Outcome::Back : navigator_.keys(keys, rows);
    const auto row = rows.at(navigator_.row());
    if (outcome == Outcome::Back || (outcome == Outcome::Activate && row == Row::Cancel)) {
        finishGainMatch(now, "Gain match cancelled"); return;
    }
    if (outcome == Outcome::Activate) {
        if (row == Row::Measure || row == Row::Swapped) { match.start(now, data_.nowUs()); showGainMatch(); return; }
        if (row == Row::Swap) { match.swap(); showGainMatch(); return; }
        if (row == Row::Save) {
            const auto status = data_.saveGain(match.target(), match.range(), match.proposed());
            if (status == DataStatus::Ok) finishGainMatch(now, "Gain saved; applies to new captures");
            else view_.notify(status == DataStatus::Busy ? "Finish the calibration first" :
                              status == DataStatus::InvalidArgument ? "Factory gain selected: saved cells would return" :
                              "Gain JSON not saved; unchanged", now);
            return;
        }
    }
    if (keys) view_.properties(rows, navigator_);
}
// While it lasts the user's settings must stay (sameSetup): a change of them, HOLD or another
// operation ends it; Measuring takes each fresh record.
void ApplicationController::serviceGainMatchUi(uint32_t now) {
    if (panel_ != Panel::GainMatch) return;
    if (shutdown_ != Shutdown::None || recoveryPending_) { finishGainMatch(now, nullptr, false); return; }
    auto &match = gainMatch();
    if (data_.gainMatchReady() != DataStatus::Ok || !match.sameSetup(data_.scopeMetadata())) {
        finishGainMatch(now, "Capture settings changed. Start again."); return;
    }
    if (match.step() != Step::Measuring) return;
    const unsigned shown = match.percent();
    {
        scope::FrameLease lease;
        if (data_.acquireScope(lease)) match.take(lease.view());
    }
    match.tick(now);
    if (match.step() != Step::Measuring) showGainMatch();
    else if (match.percent() != shown) view_.properties(ui::GainMatchProgress(match), navigator_);
}
} // namespace openosc
