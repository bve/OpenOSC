#include "openosc/application/controller.hpp"
#if OPENOSC_BOARD == 703
#include "openosc/data/decimal.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/platform/clocks.hpp"
#include <cstdio>

namespace openosc {
namespace {
using Wizard = calibration::InterleaveMatchWizard;
using Step = Wizard::Step;
using Issue = Wizard::Issue;

uint32_t coreHz() {
    const auto mode = ClockControl::activeMode();
    return mode < RunModeCount ? RunModes[mode].coreHz : 0;
}
const char *error(Issue issue) {
    switch (issue) {
    case Issue::NoFrame: return "No fresh capture. Retry.";
    case Issue::WrongFrequency: return "Check generator frequency.";
    case Issue::Level: return "Check sine level and frequency.";
    case Issue::Impure: return "Need a clean sine. Check signal.";
    case Issue::Fit: return "Fit exceeds calibration limits.";
    case Issue::Validation: return "Profile failed the tone checks.";
    default: return nullptr;
    }
}
const char *readiness(DataStatus status) {
    if (status == DataStatus::Uncalibrated) return "Calibrate Interleave offset first";
    if (status == DataStatus::InvalidArgument) return "Use CH1 Interleave, Auto and 10/25 ns";
    if (status == DataStatus::Overload) return "Reduce sine level or change CH1 scale";
    return "Enable RUN and stop GEN / other operations";
}
class MatchRows final : public ui::PropertyList {
public:
    static constexpr unsigned Primary = 3, Cancel = 4;
    explicit MatchRows(const Wizard &wizard) : wizard_(wizard) {}
    const char *title() const override { return "Interleave gain & timing"; }
    const char *notice() const override {
        if (const auto *reason = error(wizard_.issue())) return reason;
        if (wizard_.step() == Step::Reference) return "Apply the requested sine to CH1";
        if (wizard_.step() == Step::Review) return "Restart after saving to apply";
        return nullptr;
    }
    unsigned count() const override { return 5; }
    ui::PropertyKind kind(unsigned row) const override {
        return row == Cancel || (row == Primary && (wizard_.step() == Step::Reference || wizard_.step() == Step::Review))
            ? ui::PropertyKind::Action : ui::PropertyKind::Info;
    }
    void name(unsigned row, char *out, size_t capacity) const override {
        const char *name = "";
        switch (row) {
        case 0: name = "Capture rate"; break;
        case 1: name = wizard_.step() == Step::Review ? "Check tones" : "Progress"; break;
        case 2: name = wizard_.step() == Step::Review ? "Median improvement" : "Reference"; break;
        case Primary:
            switch (wizard_.step()) {
            case Step::Reference: name = "Measure tone"; break;
            case Step::Measuring: name = "Measuring..."; break;
            case Step::Review: name = "Save profile"; break;
            case Step::Failed: name = "Calibration failed"; break;
            }
            break;
        case Cancel: name = "Cancel"; break;
        }
        std::snprintf(out, capacity, "%s", name);
    }
    void value(unsigned row, char *out, size_t capacity) const override {
        if (capacity) out[0] = 0;
        if (row == 0) std::snprintf(out, capacity, "%u MS/s", unsigned(wizard_.rate().value() / 1e6));
        else if (row == 1) {
            if (wizard_.step() == Step::Review) std::snprintf(out, capacity, "%u passed", Wizard::Verification);
            else if (wizard_.frequencyMHz())
                std::snprintf(out, capacity, "%s %u/%u", wizard_.index() < Wizard::Training ? "Fit" : "Check",
                    wizard_.index() < Wizard::Training ? wizard_.index() + 1 : wizard_.index() - Wizard::Training + 1,
                    wizard_.index() < Wizard::Training ? Wizard::Training : Wizard::Verification);
        } else if (row == 2) {
            if (wizard_.step() == Step::Review) {
                char db[16];
                Decimal::fixed(wizard_.improvementDb(), 1, db, sizeof db);
                std::snprintf(out, capacity, "%s dB", db);
            } else if (wizard_.frequencyMHz())
                std::snprintf(out, capacity, "%u MHz", wizard_.frequencyMHz());
        }
    }
private:
    const Wizard &wizard_;
};
} // namespace

void ApplicationController::openInterleaveMatch(uint32_t now) {
    if (autoBusy() || state_.mode != MeasurementMode::Oscilloscope) {
        view_.notify(Tooltip::Unavailable, "Select Scope before calibration", now); return;
    }
    const auto ready = data_.interleaveMatchReady();
    if (ready != DataStatus::Ok) {
        view_.notify(ready == DataStatus::Uncalibrated || ready == DataStatus::IoError ? Tooltip::Critical : Tooltip::Unavailable,
                     ready == DataStatus::Uncalibrated && data_.scopeNominal() ? "Factory calibration required" : readiness(ready), now);
        return;
    }
    data_.interleaveMatchCalibrating(true);
    interleaveMatch_.begin(coreHz(), data_.scopeMetadata());
    state_.settingsOpen = false;
    openPanel(Panel::InterleaveMatch, MatchRows(interleaveMatch_), MatchRows::Primary);
}
void ApplicationController::showInterleaveMatch() {
    using Rows = MatchRows;
    const auto step = interleaveMatch_.step();
    navigator_.reset(step == Step::Measuring || step == Step::Failed ? Rows::Cancel : Rows::Primary);
    view_.properties(Rows(interleaveMatch_), navigator_);
}
void ApplicationController::finishInterleaveMatch(uint32_t now, Tooltip kind, const char *notice, bool returnToSettings) {
    data_.interleaveMatchCalibrating(false);
    panel_ = Panel::None;
    if (returnToSettings) {
        state_.settingsOpen = true;
        navigator_.reset(settingsMenu_.selected());
        showSettings();
    }
    if (notice) view_.notify(kind, notice, now);
}
void ApplicationController::interleaveMatchKeys(uint32_t keys, uint32_t now) {
    MatchRows rows(interleaveMatch_);
    using Outcome = ui::PropertyNavigator::Outcome;
    const auto action = (keys & Keypad::Mode) ? Outcome::Back : navigator_.keys(keys, rows);
    if (action == Outcome::Back || (action == Outcome::Activate && navigator_.row() == MatchRows::Cancel)) {
        finishInterleaveMatch(now, Tooltip::Done, "Interleave calibration cancelled"); return;
    }
    if (action == Outcome::Activate && navigator_.row() == MatchRows::Primary) {
        if (interleaveMatch_.step() == Step::Reference) {
            interleaveMatch_.startTone(now, data_.nowUs());
            showInterleaveMatch();
            return;
        }
        if (interleaveMatch_.step() == Step::Review) {
            const auto status = data_.saveInterleaveMatch(interleaveMatch_.clockHz(), interleaveMatch_.rate(),
                                                          interleaveMatch_.channel(), interleaveMatch_.filter());
            if (status == DataStatus::Ok) finishInterleaveMatch(now, Tooltip::Done, "Profile saved. Restart to apply.");
            else view_.notify(status == DataStatus::NoSpace ? Tooltip::Unavailable : Tooltip::Critical,
                              status == DataStatus::NoSpace ? "All four profile slots are used" :
                              status == DataStatus::InvalidPacket ? "Profile file invalid" :
                              "Cannot save profile. Retry.", now);
            return;
        }
    }
    if (keys) view_.properties(rows, navigator_);
}
void ApplicationController::serviceInterleaveMatchUi(uint32_t now) {
    if (panel_ != Panel::InterleaveMatch) return;
    if (shutdown_ != Shutdown::None || recoveryPending_) { finishInterleaveMatch(now, Tooltip::Done, nullptr, false); return; }
    if (data_.interleaveMatchReady() != DataStatus::Ok ||
        !interleaveMatch_.sameSetup(coreHz(), data_.scopeMetadata())) {
        finishInterleaveMatch(now, Tooltip::Advisory, "Capture settings changed. Start again."); return;
    }
    if (interleaveMatch_.step() != Step::Measuring || !interleaveMatch_.settled(now)) return;
    {
        scope::FrameLease lease;
        if (data_.acquireScope(lease) && interleaveMatch_.fresh(lease.view().metadata()))
            interleaveMatch_.take(lease.view());
    }
    if (interleaveMatch_.expired(now)) interleaveMatch_.noFrame();
    if (interleaveMatch_.step() != Step::Measuring) showInterleaveMatch();
}
} // namespace openosc
#endif
