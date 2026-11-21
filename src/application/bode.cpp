// Frequency response analysis on the ZT703S (GitHub #72): Analyze -> Bode (FRA). The sweep itself is
// scope::BodeSweep (acquisition/bode.cpp), its results scope::BodeResults (acquisition/bode_analysis.cpp),
// its CSV BodeCsvFile (bode_csv_file.cpp); here its session, the instrument behind it and the UI.
// - The generator and the capture are set through DataAccess directly, never applyGenerator/applyScope:
//   no preference changes, no saves, and GeneratorStartup follows no request (the output's ON/OFF is
//   not the user's). The sweep plays a sine at the drive chosen in its setup (the generator's own level,
//   or Dedicated/Low/High, or Custom DAC codes), set at Run only.
// - The original capture request and generator settings and output state are kept when the sweep starts
//   and put back when it ends (done, stopped, failed) and whenever the panel goes: Exit/MODE, a mode
//   change by any path (selectMode), a shutdown (requestShutdown), Remote (setRemote), the Scope taken
//   over (HOLD, a recording, a calibration, AutoSetup), recovery. An original OFF output is switched off
//   before anything else (the old excitation never plays again); a restore a busy instrument refused
//   keeps the originals and is tried again (RetryMs) until it is whole. While the sweep's settings are in
//   force (bodeOwns) the SCPI paths that would change them or hold its records are refused, and a mode or
//   Remote change waits for the restore.
// - The run as it ran (its settings, the drive that played) is kept apart from the setup: the results,
//   the Results page and the CSV are of it, whatever the setup says after. While its CSV is written the
//   points stay as they are: no new sweep starts.
#include "openosc/application/controller.hpp"
#if OPENOSC_BOARD == 703
#include "openosc/acquisition/bode.hpp"
#include "openosc/acquisition/bode_analysis.hpp"
#include "openosc/application/bode_csv_file.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/generator/frequency.hpp"
#include "openosc/platform/input.hpp"
#include "openosc/ui/editors/scope_actions.hpp"
#include "openosc/ui/scope/format.hpp"
#include <algorithm>
#include <cstdio>
#include <new>
#include <utility>

namespace openosc {
namespace {
using Sweep = scope::BodeSweep;
using Settings = scope::BodeSettings;
using Drive = ui::BodeDrive;
// The session: ITCM (.bode, zeroed at reset), built in place on first use. Nothing of it in DTCM.
struct BodeSession {
    Settings settings;
    Sweep sweep;
    scope::BodePoint points[Settings::MaximumPoints];
    ScopeRequest original;
    generator::Settings generator;
    bool generatorOn = false, touched = false;
    scope::BodeCursor cursor;  // The active cursor's point, the named marker, Plot or Table.
    unsigned other = 0;        // The other cursor's point.
    bool second = false;       // B active (A otherwise): MENU swaps them.
    bool results = false;      // The Results page (over Plot/Table).
    bool setupFromResult = false;
    Drive drive = Drive::FromGen;
    uint16_t customLow = 0, customHigh = 1600; // Custom DAC codes (the Low level's span by default).
    // The run as it ran: its settings and the drive that played (Results, CSV), its results.
    Settings run;
    generator::Settings excitation;
    Drive runDrive = Drive::FromGen;
    scope::BodeResults analysis;
    bool saving = false;       // A CSV of it being written: no new sweep.
    unsigned measured = 0;
    uint32_t timerHz = 0;      // The DAC timer's clock when the setup opened.
    uint64_t maximum = 0;      // Millihertz: the sine's limit at that clock and the chosen drive's level.
    uint32_t retryAt = 0;      // A refused restore's next attempt (ms).
    unsigned attempts = 0;     // Refused restores in a row: RetryLimit of them give up (a fault, not Busy).
    bool abandoned = false;    // Given up: said once.
    char status[24] = "";
};
constexpr uint32_t RetryMs = 50;
constexpr unsigned RetryLimit = 100; // 5 s of refusals: the instrument is not busy but faulty.
constexpr int32_t CodeMax = 4095, CodeCoarse = 100; // Custom DAC codes: 0..4095, Left/Right 1, Up/Down 100.
alignas(8) __attribute__((section(".bode"))) unsigned char storage[sizeof(BodeSession)];
bool built = false;
BodeSession &session() {
    if (!built) { new (storage) BodeSession(); built = true; }
    return *reinterpret_cast<BodeSession *>(storage);
}
// The frequencies offered for the start and the stop: 1-2-5 from 1 Hz below the limit, then the limit.
constexpr uint64_t Steps[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000,
                              10000000, 20000000, 50000000, 100000000, 200000000};
unsigned frequencies(uint64_t maximum) {
    unsigned n = 0;
    while (n < sizeof Steps / sizeof *Steps && Steps[n] < maximum) ++n;
    return n + 1;
}
uint64_t frequencyAt(unsigned index, uint64_t maximum) {
    return index < sizeof Steps / sizeof *Steps && Steps[index] < maximum ? Steps[index] : maximum;
}
unsigned indexOf(uint64_t millihertz, uint64_t maximum) {
    const unsigned n = frequencies(maximum);
    for (unsigned i = 0; i < n; ++i) if (frequencyAt(i, maximum) >= millihertz) return i;
    return n - 1;
}
template <size_t N> unsigned choiceOf(const uint32_t (&values)[N], uint32_t value) {
    for (unsigned i = 0; i < N; ++i) if (values[i] == value) return i;
    return 0;
}
// The sine a drive plays: the generator's own settings (From GEN), or them at another level; Custom with
// the setup's codes (Levels::codes: what WaveTable spans).
generator::Settings excitationOf(const generator::Settings &gen, Drive drive, uint16_t low, uint16_t high) {
    auto s = gen;
    s.shape = generator::Shape::Sine;
    if (drive == Drive::Dedicated) s.level = generator::Level::Dedicated;
    else if (drive == Drive::Low) s.level = generator::Level::Low;
    else if (drive == Drive::High) s.level = generator::Level::High;
    else if (drive == Drive::Custom) { s.level = generator::Level::Custom; s.minimumCode = low; s.maximumCode = high; }
    return s;
}
// The sine's highest frequency at the level a drive plays, at the clock the setup saw.
void limit(BodeSession &s, const generator::Settings &gen) {
    const auto level = excitationOf(gen, s.drive, s.customLow, s.customHigh).level;
    s.maximum = generator::DacCapabilities::maximumMillihertz(generator::Shape::Sine, level, s.timerHz);
    if (s.settings.stopMillihertz > s.maximum) s.settings.stopMillihertz = s.maximum;
    if (s.settings.startMillihertz > s.maximum) s.settings.startMillihertz = s.maximum;
}
// The generator and the capture for the sweep (BodePort), straight through DataAccess.
class Instrument final : public scope::BodePort {
public:
    Instrument(DataAccess &data, BodeSession &s) : data_(data), s_(s) {}
    bool tune(uint64_t millihertz, double &hz) override {
        auto settings = s_.excitation; // The drive chosen at Run.
        settings.frequencyHz = generator::Frequency::rational(millihertz);
        auto &output = data_.generator();
        s_.touched = true;
        if (!output.configure(settings)) return false;
        if (!output.enabled() && !output.enable()) return false;
        const auto snapshot = output.snapshot();
        if (snapshot.status != DataStatus::Ok || !snapshot.applied.actualHz.valid()) return false;
        hz = snapshot.applied.actualHz.value(); // What the DAC plays, not the setting.
        return hz > 0;
    }
    bool configure(const ScopeRequest &request) override { s_.touched = true; return data_.configureScope(request); }
    uint64_t nowUs() const override { return data_.nowUs(); }
    bool frame(scope::FrameLease &lease) override { return data_.acquireScope(lease); }
private:
    DataAccess &data_;
    BodeSession &s_;
};
// SETUP: the range, the density, the settle, the averages, the drive (its codes when Custom), Run; with a
// result: the plot, the Results page and its CSV. Rows shown are listed (Custom's codes only for Custom).
// Custom's codes are numbers over the DAC's whole range, each kept inside its pair (low 0..high-1, high
// low+1..4095): the pair always spans, no edit moves the other code (a cancelled edit restores exactly).
class Rows final : public ui::PropertyList {
public:
    enum Row : uint8_t { Start, Stop, Density, Settle, Averages, Level, LowCode, HighCode, Run, Plot, Results, SaveCsv, Count };
    Rows(BodeSession &s, const generator::Settings &gen) : s_(s), gen_(gen) {
        for (unsigned r = 0; r < Count; ++r) {
            const bool custom = r == LowCode || r == HighCode, result = r >= Plot;
            if ((custom && s.drive != Drive::Custom) || (result && !s.measured)) continue;
            rows_[n_++] = Row(r);
        }
    }
    Row at(unsigned index) const { return index < n_ ? rows_[index] : Count; }
    unsigned index(Row row) const { for (unsigned i = 0; i < n_; ++i) if (rows_[i] == row) return i; return 0; }
    const char *title() const override { return "BODE (FRA)"; }
    const char *notice() const override { return "CH1: DUT input, CH2: DUT output"; }
    unsigned count() const override { return n_; }
    ui::PropertyKind kind(unsigned i) const override {
        const auto row = at(i);
        return row == LowCode || row == HighCode ? ui::PropertyKind::Number : row < Run ? ui::PropertyKind::Choice : ui::PropertyKind::Action;
    }
    void value(unsigned i, char *out, size_t capacity) const override {
        if (kind(i) == ui::PropertyKind::Number) std::snprintf(out, capacity, "%ld", long(number(i)));
        else PropertyList::value(i, out, capacity);
    }
    int32_t number(unsigned i) const override { return at(i) == LowCode ? s_.customLow : at(i) == HighCode ? s_.customHigh : 0; }
    bool range(unsigned i, int32_t &low, int32_t &high) const override {
        if (at(i) == LowCode) { low = 0; high = s_.customHigh - 1; return true; }
        if (at(i) == HighCode) { low = s_.customLow + 1; high = CodeMax; return true; }
        return false;
    }
    void setNumber(unsigned i, int32_t value) override {
        int32_t low, high;
        if (!range(i, low, high)) return;
        (at(i) == LowCode ? s_.customLow : s_.customHigh) = uint16_t(std::clamp(value, low, high));
    }
    void adjust(unsigned i, int delta) override { setNumber(i, number(i) + delta); }
    int coarseStep(unsigned) const override { return CodeCoarse; }
    void name(unsigned i, char *out, size_t capacity) const override {
        static constexpr const char *Names[] = {"Start", "Stop", "Points/decade", "Settle", "Averages", "Drive",
                                                "Custom low code", "Custom high code", "Run sweep", "Show plot", "Results", "Save CSV"};
        std::snprintf(out, capacity, "%s", at(i) < Count ? Names[at(i)] : "");
    }
    unsigned options(unsigned i) const override {
        switch (at(i)) {
        case Start: case Stop: return frequencies(s_.maximum);
        case Density: return sizeof Settings::PointsChoices / sizeof *Settings::PointsChoices;
        case Settle: return sizeof Settings::SettleChoices / sizeof *Settings::SettleChoices;
        case Averages: return sizeof Settings::AverageChoices / sizeof *Settings::AverageChoices;
        case Level: return unsigned(Drive::Custom) + 1;
        default: return 0;
        }
    }
    unsigned option(unsigned i) const override {
        switch (at(i)) {
        case Start: return indexOf(s_.settings.startMillihertz, s_.maximum);
        case Stop: return indexOf(s_.settings.stopMillihertz, s_.maximum);
        case Density: return choiceOf(Settings::PointsChoices, s_.settings.pointsPerDecade);
        case Settle: return choiceOf(Settings::SettleChoices, s_.settings.settleMs);
        case Averages: return choiceOf(Settings::AverageChoices, s_.settings.averages);
        case Level: return unsigned(s_.drive);
        default: return 0;
        }
    }
    void optionName(unsigned i, unsigned index, char *out, size_t capacity) const override {
        switch (at(i)) {
        case Start: case Stop: plot::Format::quantity(frequencyAt(index, s_.maximum) / 1000.0, "Hz", out, capacity, 3); break;
        case Density: std::snprintf(out, capacity, "%u", unsigned(Settings::PointsChoices[index % 4])); break;
        case Settle: std::snprintf(out, capacity, "%u ms", unsigned(Settings::SettleChoices[index % 8])); break;
        case Averages: std::snprintf(out, capacity, "%u", unsigned(Settings::AverageChoices[index % 3])); break;
        case Level:
            if (Drive(index) == Drive::FromGen) { // "GEN: Low": the generator's own level, as set now.
                char level[24]; generator::LevelText::name(gen_, level, sizeof level);
                std::snprintf(out, capacity, "GEN: %s", level);
            } else std::snprintf(out, capacity, "%s", ui::BodeView::driveName(Drive(index)));
            break;
        default: if (capacity) out[0] = 0;
        }
    }
    bool choose(unsigned i, unsigned index) override {
        auto &t = s_.settings;
        switch (at(i)) {
        case Start: t.startMillihertz = frequencyAt(index, s_.maximum); return true;
        case Stop: t.stopMillihertz = frequencyAt(index, s_.maximum); return true;
        case Density: if (index >= 4) return false; t.pointsPerDecade = Settings::PointsChoices[index]; return true;
        case Settle: if (index >= 8) return false; t.settleMs = Settings::SettleChoices[index]; return true;
        case Averages: if (index >= 3) return false; t.averages = Settings::AverageChoices[index]; return true;
        case Level:
            if (index > unsigned(Drive::Custom)) return false;
            s_.drive = Drive(index); limit(s_, gen_); return true;
        default: return false;
        }
    }
private:
    BodeSession &s_;
    const generator::Settings &gen_;
    Row rows_[Count]{};
    unsigned n_ = 0;
};
// The instrument as it was before the sweep; true once all of it is back (or nothing was changed). The
// sweep's lease goes first; an original OFF is switched off before its settings return (a configure
// while off plays nothing), an original ON gets its settings (while on: the stream restarts with them)
// and is switched on. The capture's request returns (staged for the next RUN while HOLD keeps a record).
// Every step is idempotent: a refused one (Busy) leaves `touched` and the originals for the next try.
bool restore(DataAccess &data, BodeSession &s, uint32_t now) {
    s.sweep.cancel();
    if (!s.touched) return true;
    auto &output = data.generator();
    bool generator = true;
    if (!s.generatorOn) {
        if (output.enabled()) output.disable(); // Not when off: disable() would end the calibration reference.
        generator = output.offConfirmed() && output.configure(s.generator);
    } else generator = output.configure(s.generator) && output.enable();
    const bool scope = data.scopeRequest().same(s.original) ||
        (data.scopeHeld() ? data.stageScope(s.original) : data.configureScope(s.original));
    if (generator && scope) { s.touched = false; s.attempts = 0; return true; }
    if (++s.attempts >= RetryLimit) { s.touched = false; s.attempts = 0; s.abandoned = true; } // Never a lock for ever.
    s.retryAt = now + RetryMs;
    return false;
}
// The sweep's end in the header: how many of its points are valid of those measured (and of all).
void ended(BodeSession &s, const char *word) {
    unsigned valid = 0;
    for (unsigned i = 0; i < s.measured; ++i) valid += s.points[i].valid();
    if (s.measured == s.sweep.count()) std::snprintf(s.status, sizeof s.status, "%s: %u/%u valid", word, valid, s.measured);
    else std::snprintf(s.status, sizeof s.status, "%s %u/%u, %u valid", word, s.measured, s.sweep.count(), valid);
}
const char *failure(Sweep::Failure f) {
    return f == Sweep::Failure::Generator ? "Bode stopped: generator refused the frequency" : "Bode stopped: capture refused";
}
} // namespace

bool ApplicationController::bodeRunning() const { return built && panel_ == Panel::Bode && session().sweep.running(); }
bool ApplicationController::bodeOwns() const { return built && (session().touched || session().sweep.running()); }
void ApplicationController::openBode(uint32_t now) {
    const auto &request = data_.scopeRequest();
    const char *why = state_.mode != MeasurementMode::Oscilloscope || remote_ ? "Bode needs the Scope (not Remote)"
        : data_.scopeHeld() ? "Press RUN first: Bode needs a running capture"
        : !request.frontend.enabled[0] || !request.frontend.enabled[1] ? "Enable CH1 and CH2 for Bode"
        : data_.scopeMetadata().layout == scope::Layout::InterleavedCh1 ? "Turn Interleave CH1 off for Bode"
        : data_.scopeRecording() || data_.zeroBusy() || autoBusy() ? "Finish the running operation first"
        : data_.generator().status() == DataStatus::HardwareError ? "Generator fault: switch it off and on"
        : nullptr;
    if (why) { view_.notify(why, now); return; }
    auto &s = session();
    if (!restore(data_, s, now)) { view_.notify("Bode: the last sweep's restore is pending", now); return; } // Never two sessions' originals.
    const auto &generator = data_.generator();
    s.timerHz = generator.snapshot().timerHz; // The DAC timer's clock as the output last read it.
    if (!s.timerHz) { view_.notify("Generator clock unknown: switch GEN on and off", now); return; }
    limit(s, generator.settings());
    panel_ = Panel::Bode;
    s.cursor.table = false; s.cursor.marker = scope::BodeMarkers::None; s.results = false; // The plot first.
    s.setupFromResult = false;
    showBode(false);
}
void ApplicationController::showBode(bool plot) {
    auto &s = session();
    if (!plot) {
        view_.bode().hide();
        const Rows rows(s, data_.generator().settings());
        navigator_.reset(rows.index(Rows::Run));
        view_.properties(rows, navigator_);
        return;
    }
    ui::BodeViewData d;
    d.points = s.points; d.count = s.sweep.count(); d.measured = s.measured;
    d.cursor = s.cursor; d.other = s.other; d.second = s.second; d.results = s.results;
    d.running = s.sweep.running();
    d.markers = scope::BodeMarkers::of(s.points, s.measured);
    d.analysis = &s.analysis; d.run = &s.run; d.excitation = &s.excitation; d.drive = s.runDrive;
    d.status = s.status;
    d.startHz = s.sweep.grid().start / 1000.0; d.stopHz = s.sweep.grid().stop / 1000.0;
    view_.bodePlot(d); // The host's overlay: nothing of the Scope drawn under it.
}
void ApplicationController::finishBode(uint32_t now, const char *notice) {
    if (panel_ != Panel::Bode) return;
    if (!restore(data_, session(), now)) notice = "Bode: instrument busy, restoring"; // serviceBode tries again.
    view_.bode().hide();
    closePanel();
    dataRefresh_ = true;
    if (notice) view_.notify(notice, now);
}
void ApplicationController::startBodeSweep(uint32_t now) {
    auto &s = session();
    if (s.saving || bodeCsvFile().busy()) { view_.notify("Bode: saving the CSV, the next sweep after it", now); return; }
    // The DAC clock and the generator's level as they are now (a host may have changed either since the
    // setup opened, e.g. over SCPI while the result showed): the limit, then the grid, from them.
    s.timerHz = data_.generator().snapshot().timerHz;
    if (!s.timerHz) { view_.notify("Generator clock unknown: switch GEN on and off", now); return; }
    limit(s, data_.generator().settings());
    const auto grid = scope::BodeGrid::of(s.settings, 1000, s.maximum);
    if (!grid.valid()) { view_.notify("Bode: Stop must be above Start", now); return; }
    if (!restore(data_, s, now)) { view_.notify("Bode: the last sweep's restore is pending", now); return; }
    s.original = data_.scopeRequest();
    s.generator = data_.generator().settings();
    s.generatorOn = data_.generator().enabled();
    const auto excitation = excitationOf(s.generator, s.drive, s.customLow, s.customHigh);
    if (!excitation.valid()) { view_.notify("Bode: the drive's codes are not valid", now); return; }
    if (!s.sweep.start(s.settings, grid, s.original, s.points)) { view_.notify("Bode: cannot start", now); return; }
    s.run = s.settings; s.excitation = excitation; s.runDrive = s.drive; // The run as it runs.
    s.analysis = {};
    s.measured = 0; s.cursor.point = 0; s.other = 0; s.second = false; s.cursor.marker = scope::BodeMarkers::None;
    std::snprintf(s.status, sizeof s.status, "Sweep 0/%u", grid.count);
    showBode(true);
}
// Setup's Save CSV: the run as it ran, its points (unchanged while it is written: no sweep starts).
void ApplicationController::saveBodeCsv(uint32_t now) {
    auto &s = session();
    auto &csv = bodeCsvFile();
    // No new file once a power off, restart or Recovery is under way (or queued): as saveDecoderCsv.
    if (shutdown_ != Shutdown::None || shutdownSequence_.active() || recoveryPending_) { view_.notify("Bode: no CSV while shutting down", now); return; }
    if (csv.busy()) { view_.notify("Bode: a CSV is being saved", now); return; }
    if (s.sweep.running() || !s.measured) { view_.notify("Bode: nothing finished to save", now); return; }
    BodeCsvRun run;
    run.settings = s.run; run.grid = s.sweep.grid(); run.excitation = s.excitation;
    run.points = s.points; run.count = s.measured; run.complete = s.sweep.state() == Sweep::State::Done && s.measured == s.sweep.count();
    if (!csv.start(fs_, run)) {
        char text[64]; std::snprintf(text, sizeof text, "Bode CSV not saved: %s", csv.error() ? csv.error() : "refused");
        view_.notify(text, now);
        return;
    }
    s.saving = true;
    view_.notify("Bode: saving CSV...", now);
}
void ApplicationController::bodeKeys(const KeyEvents &events, uint32_t keys, uint32_t now) {
    auto &s = session();
    if (events.shortPress & Keypad::Mode) { finishBode(now, "Bode closed, Scope restored"); return; }
    if (!view_.bode().visible()) { // SETUP rows.
        Rows rows(s, data_.generator().settings());
        using Outcome = ui::PropertyNavigator::Outcome;
        const auto outcome = navigator_.keys(keys, rows);
        if (outcome == Outcome::Back) {
            if (s.setupFromResult) showBode(true);
            else openPanel(Panel::Analyze, ui::ScopeActions(view_.waveform().options()), ui::ScopeActions::Bode);
            return;
        }
        if (outcome == Outcome::Activate) {
            const auto row = rows.at(navigator_.row());
            if (row == Rows::Run) { startBodeSweep(now); return; }
            if (row == Rows::Plot || row == Rows::Results) { s.results = row == Rows::Results; showBode(true); return; }
            if (row == Rows::SaveCsv) saveBodeCsv(now);
        }
        const Rows shown(s, data_.generator().settings()); // A drive's change shows or hides Custom's codes.
        view_.properties(shown, navigator_);
        return;
    }
    // The result: F1 Run/Stop, F2 setup, F3 Plot/Table (Results: back to the plot), F4 exit; MENU swaps the
    // active cursor (A/B); Left/Right move it one point; Up/Down the named markers (Plot) or a page (Table).
    if (keys & Keypad::F4) { finishBode(now, "Bode closed, Scope restored"); return; }
    if (keys & Keypad::F1) {
        if (bodeRunning()) {
            restore(data_, s, now);
            ended(s, "Stop");
        } else { startBodeSweep(now); return; }
    }
    if ((keys & Keypad::F2) && !bodeRunning()) {
        s.setupFromResult = true;
        showBode(false);
        return;
    }
    auto &cursor = s.cursor;
    if (keys & Keypad::Menu) { std::swap(cursor.point, s.other); s.second = !s.second; cursor.marker = scope::BodeMarkers::None; }
    if (keys & Keypad::F3) {
        if (s.results) { s.results = false; cursor.table = false; }
        else cursor.table = !cursor.table; // The same points in the other form.
    }
    if (!s.results) {
        if (keys & Keypad::Left) cursor.step(-1, s.measured);
        if (keys & Keypad::Right) cursor.step(1, s.measured);
        if (keys & (Keypad::Up | Keypad::Down)) {
            const bool up = keys & Keypad::Up;
            if (cursor.table) cursor.page(up ? -1 : 1, s.measured);
            else if (!cursor.jump(up, s.points, s.measured)) view_.notify("No markers in this sweep yet", now);
        }
    }
    showBode(true);
}
void ApplicationController::serviceBode(uint32_t now) {
    // The CSV's writer: a bounded step a pass, whatever is on show; its outcome said once.
    auto &csv = bodeCsvFile();
    csv.service();
    if (!built) return;
    auto &s = session();
    if (s.saving && !csv.busy()) {
        s.saving = false;
        char text[64];
        if (csv.savedPath()) std::snprintf(text, sizeof text, "Bode CSV saved: %s", csv.savedPath());
        else std::snprintf(text, sizeof text, "Bode CSV not saved: %s", csv.error() ? csv.error() : "cancelled");
        view_.notify(text, now);
    }
    // A refused restore again (after the panel went too: a path that closed it); not while the instrument
    // shuts down (it stops everything itself and keeps the user's preferences, which the sweep never changed).
    const bool retry = s.touched && !s.sweep.running() && int32_t(now - s.retryAt) >= 0 && shutdown_ == Shutdown::None;
    if (retry) restore(data_, s, now);
    if (s.abandoned) { s.abandoned = false; view_.notify("Bode: restore refused: check GEN and the Scope", now); }
    if (panel_ != Panel::Bode) return;
    const char *taken = shutdown_ != Shutdown::None || recoveryPending_ ? "" : remote_ ? "Bode stopped: USB Remote"
        : state_.mode != MeasurementMode::Oscilloscope ? "" : nullptr;
    if (taken) { finishBode(now, *taken ? taken : nullptr); return; }
    if (!s.sweep.running()) return;
    if (data_.scopeHeld() || data_.scopeRecording() || data_.zeroBusy() || autoBusy()) {
        restore(data_, s, now);
        ended(s, "Stop");
        view_.notify("Bode stopped: the capture was taken over", now);
        showBode(true);
        return;
    }
    decoder_.dropHostRecord(); // A host's decoded record would hold the capture's one slot.
    Instrument port(data_, s);
    s.sweep.step(port);
    if (s.sweep.index() != s.measured) {
        const unsigned before = s.measured;
        s.measured = s.sweep.index();
        s.cursor.follow(before, s.measured); // On the newest point: the next one; placed elsewhere: stays.
        s.analysis = scope::BodeResults::of(s.points, s.measured);
        std::snprintf(s.status, sizeof s.status, "Sweep %u/%u", s.measured, s.sweep.count());
        if (view_.bode().visible()) showBode(true);
    }
    if (s.sweep.state() == Sweep::State::Done) {
        restore(data_, s, now);
        ended(s, "Done");
        showBode(true);
    } else if (s.sweep.state() == Sweep::State::Failed) {
        const auto f = s.sweep.failure();
        restore(data_, s, now);
        ended(s, "Fail");
        view_.notify(failure(f), now);
        showBode(true);
    }
}
} // namespace openosc
#endif
