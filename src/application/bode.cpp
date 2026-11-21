// Frequency response analysis (GitHub #72): Analyze -> Bode (FRA), on the ZT703S (DAC2) and the ZT706S
// (the USART3 module, ModuleOutput), one session and one controller for both. The sweep itself is
// scope::BodeSweep (acquisition/bode.cpp), its results scope::BodeResults (acquisition/bode_analysis.cpp),
// its CSV BodeCsvFile (bode_csv_file.cpp); here its session, the instrument behind it and the UI.
// - The generator and the capture are set through DataAccess directly, never applyGenerator/applyScope:
//   no preference changes, no saves, and GeneratorStartup follows no request (the output's ON/OFF is
//   not the user's). The sweep plays a sine at the drive chosen in its setup, set at Run only: ZT703S the
//   generator's own level, or Dedicated/Low/High, or Custom DAC codes; ZT706S the generator's amplitude
//   (the module has no DAC codes or level profiles: Drive is that one choice, no Custom rows).
// - The generator is asked once a point (BodePort::tune) and polled (tuned): the DAC is confirmed at
//   once, the module only by its acknowledge of exactly that frame with the output ON; the settle counts
//   from it. Its bounded retries are ModuleRecovery's; Failed or MaximumTuneMs fails the sweep.
// - ZT706S gating: the module's control-word bit 10 is in every Scope word (frontend 0x1600 and the
//   channels' words); only the DMM word needs generator::controlBits. Bode runs in the Scope only: no
//   enable of its own.
// - The original capture request and generator settings and output state are kept when the sweep starts
//   and put back when it ends (done, stopped, failed) and whenever the panel goes: Exit/MODE, a mode
//   change by any path (selectMode), a shutdown (requestShutdown), Remote (setRemote), the Scope taken
//   over (HOLD, a recording, a calibration, AutoSetup), recovery. An original OFF output is switched off
//   before anything else (the old excitation never plays again). The generator's original is asked once
//   and then polled until confirmed (ON acknowledged with the original settings, or OFF confirmed): a
//   request refused is asked again (RetryMs, RetryLimit times), one the generator gave up on (or that
//   MaximumTuneMs did not confirm) ends the restore with a notice. The capture's request is tried again
//   (RetryMs) while refused. Until the restore is whole (bodeOwns) the SCPI paths that would change the
//   generator or the capture or hold its records are refused, no new sweep starts, and a mode or Remote
//   change waits for it.
// - The run as it ran (its settings, the drive that played) is kept apart from the setup: the results,
//   the Results page and the CSV are of it, whatever the setup says after. While its CSV is written the
//   points stay as they are: no new sweep starts.
#include "openosc/application/controller.hpp"
#include "openosc/acquisition/bode.hpp"
#include "openosc/acquisition/bode_analysis.hpp"
#include "openosc/application/bode_csv_file.hpp"
#include "openosc/generator/capabilities.hpp"
#include "openosc/generator/frequency.hpp"
#include "openosc/generator/module_link.hpp"
#include "openosc/generator/module_recovery.hpp"
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
    uint32_t timerHz = 0;      // ZT703S: the DAC timer's clock when the setup opened (the module has none).
    uint64_t maximum = 0;      // Millihertz: the sine's limit for the chosen drive (Drives::maximum).
    uint64_t tuning = 0;       // Millihertz: the frequency asked of the generator (tuned() confirms it).
    uint32_t retryAt = 0;      // A refused restore's next attempt (ms).
    uint32_t askedAt = 0;      // When the generator's original was asked (ms): MaximumTuneMs to confirm it.
    unsigned attempts = 0;     // Refused restores in a row: RetryLimit of them give up (a fault, not Busy).
    bool asked = false;        // The generator's original asked (and not refused): polled, never sent again.
    const char *abandoned = nullptr; // Given up, and why: said once.
    char status[24] = "";
};
constexpr uint32_t RetryMs = 50;
constexpr unsigned RetryLimit = 100; // 5 s of refusals: the instrument is not busy but faulty.
// The module's own bound (an exchange from submit, ModuleRecovery's rounds and their delays) under the
// sweep's backstop for a confirmation, which the restore uses too.
static_assert(board::Current::Features.generator != board::GeneratorDriver::SerialModule ||
              (generator::ModuleRecovery::Rounds + 1) * generator::ModuleLink::ExchangeMs +
              generator::ModuleRecovery::Rounds * generator::ModuleRecovery::DelayMs < Sweep::MaximumTuneMs,
              "A sweep's confirmation deadline below the module's own bounded retries");
// A request the generator took, from its driver's state alone (both drivers: the DAC confirms at once,
// the module by its acknowledge): Done once that state is confirmed, Pending while a frame or a recovery
// round is under way, Failed once the driver stopped trying. Reads only: never a send.
enum class Confirmation : uint8_t { Pending, Done, Failed };
template <class Output> Confirmation confirmation(const Output &output, bool on) {
    if (on ? output.onConfirmed() : output.offConfirmed()) return Confirmation::Done;
    if (output.busy() || output.recovering() || output.status() == DataStatus::Busy) return Confirmation::Pending;
    return Confirmation::Failed;
}
constexpr int32_t CodeMax = 4095, CodeCoarse = 100; // Custom DAC codes: 0..4095, Left/Right 1, Up/Down 100.
alignas(8) __attribute__((section(".bode"))) unsigned char storage[sizeof(BodeSession)];
bool built = false;
BodeSession &session() {
    if (!built) { new (storage) BodeSession(); built = true; }
    return *reinterpret_cast<BodeSession *>(storage);
}
// The frequencies offered for the start and the stop: 1-2-5 from 1 Hz below the limit, then the limit.
// The ZT703S's limit is below 500 kHz (DacCapabilities: 344 kHz at most), so the steps above it are the module's.
constexpr uint64_t Steps[] = {1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000,
                              10000000, 20000000, 50000000, 100000000, 200000000, 500000000, 1000000000, 2000000000,
                              5000000000};
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
// The drives a board's generator offers and what each plays (one place for the board's difference):
// - ZT703S (Levels): the generator's own settings (From GEN), or them at another level; Custom with the
//   setup's codes (Levels::codes: what WaveTable spans). The limit: the DAC's for that level at the
//   timer clock the setup saw, which must be known.
// - ZT706S (the module's amplitude): From GEN only, the generator's amplitude as set. The limit: the
//   highest sine the module represents at that amplitude without lowering it; no clock of its own.
struct Drives {
    static constexpr bool Levels = generator::Capabilities::Levels;
    static constexpr unsigned Count = Levels ? unsigned(Drive::Custom) + 1 : 1;
    static constexpr bool NeedsClock = Levels;
    static generator::Settings excitation(const generator::Settings &gen, Drive drive, uint16_t low, uint16_t high) {
        auto s = gen;
        s.shape = generator::Shape::Sine;
        if (!Levels) return s;
        if (drive == Drive::Dedicated) s.level = generator::Level::Dedicated;
        else if (drive == Drive::Low) s.level = generator::Level::Low;
        else if (drive == Drive::High) s.level = generator::Level::High;
        else if (drive == Drive::Custom) { s.level = generator::Level::Custom; s.minimumCode = low; s.maximumCode = high; }
        return s;
    }
    static uint64_t maximum(const generator::Settings &excitation, uint32_t timerHz) {
        if (Levels) return generator::DacCapabilities::maximumMillihertz(generator::Shape::Sine, excitation.level, timerHz);
        return generator::ModuleCapabilities::sineMaximumMillihertz(excitation.amplitudeMillivolts);
    }
    // Why a drive cannot play at all (nullptr: it can).
    static const char *refusal(const generator::Settings &excitation) {
        if (!excitation.valid()) return "Bode: the drive's codes are not valid";
        if (!Levels && !excitation.amplitudeMillivolts) return "Bode: GEN amplitude is 0 V";
        return nullptr;
    }
};
generator::Settings excitationOf(const BodeSession &s, const generator::Settings &gen) {
    return Drives::excitation(gen, s.drive, s.customLow, s.customHigh);
}
// The sine's highest frequency for the drive chosen, at the clock the setup saw (ZT703S).
void limit(BodeSession &s, const generator::Settings &gen) {
    s.maximum = Drives::maximum(excitationOf(s, gen), s.timerHz);
    if (s.settings.stopMillihertz > s.maximum) s.settings.stopMillihertz = s.maximum;
    if (s.settings.startMillihertz > s.maximum) s.settings.startMillihertz = s.maximum;
}
// The generator's timer clock the limits need: true with it read (or none needed, the ZT706S module).
bool readClock(DataAccess &data, BodeSession &s) {
    s.timerHz = data.generator().snapshot().timerHz; // The DAC timer's clock as the output last read it.
    return !Drives::NeedsClock || s.timerHz;
}
// The generator and the capture for the sweep (BodePort), straight through DataAccess.
class Instrument final : public scope::BodePort {
public:
    Instrument(DataAccess &data, BodeSession &s) : data_(data), s_(s) {}
    bool tune(uint64_t millihertz) override {
        auto &output = data_.generator();
        s_.touched = true;
        s_.tuning = millihertz;
        const auto settings = asked();
        if (!output.configure(settings)) return false; // Changed while ON: the module's frame is queued.
        return output.enabled() || output.enable();    // OFF before: ON (with them) is queued.
    }
    Tuning tuned(double &hz) override {
        const auto &output = data_.generator();
        if (!output.enabled() || !output.settings().same(asked())) return Tuning::Failed; // Not the sweep's any more.
        switch (confirmation(output, true)) {
        case Confirmation::Pending: return Tuning::Pending;
        case Confirmation::Failed: return Tuning::Failed;
        case Confirmation::Done: break;
        }
        const auto &actual = output.plan().actualHz; // What it plays (the DAC's plan, the module's frame), not the setting.
        if (!actual.valid()) return Tuning::Failed;
        hz = actual.value();
        return hz > 0 ? Tuning::Ready : Tuning::Failed;
    }
    bool configure(const ScopeRequest &request) override { s_.touched = true; return data_.configureScope(request); }
    uint64_t nowUs() const override { return data_.nowUs(); }
    bool frame(scope::FrameLease &lease) override { return data_.acquireScope(lease); }
private:
    DataAccess &data_;
    BodeSession &s_;
    generator::Settings asked() const { // The drive chosen at Run at the frequency asked.
        auto settings = s_.excitation;
        settings.frequencyHz = generator::Frequency::rational(s_.tuning);
        return settings;
    }
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
        case Level: return Drives::Count;
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
            if (index >= Drives::Count) return false;
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
// sweep's lease goes first. The generator's original is asked once: an original OFF is switched off
// before its settings return (a configure while off plays nothing: the DAC halted, the module's
// settings kept for its next ON), an original ON gets its settings (while on: the DAC restarts with
// them, the module's frame is queued) and is switched on. Then it is only polled (confirmation(): no
// disable() or enable() again, which after a module failure would be a new request): a refused ask is
// asked again (RetryMs, RetryLimit), a request the driver gave up on or MaximumTuneMs did not confirm
// ends the restore (abandoned, said once). The capture's request returns (staged for the next RUN while
// HOLD keeps a record), tried again while refused. Ownership (`touched`) ends only with all of it.
bool restore(DataAccess &data, BodeSession &s, uint32_t now) {
    s.sweep.cancel();
    if (!s.touched) return true;
    auto &output = data.generator();
    bool refused = false;
    if (!s.asked) {
        if (!s.generatorOn) {
            if (output.enabled()) output.disable(); // Not when off: disable() would end the calibration reference.
            s.asked = !output.enabled() && output.configure(s.generator);
        } else s.asked = output.configure(s.generator) && output.enable();
        if (s.asked) s.askedAt = now;
        else refused = true;
    }
    bool generator = false;
    if (s.asked) {
        const bool kept = output.enabled() == s.generatorOn && output.settings().same(s.generator);
        const auto state = kept ? confirmation(output, s.generatorOn) : Confirmation::Failed;
        if (state == Confirmation::Done) generator = true;
        else if (state == Confirmation::Failed || now - s.askedAt >= Sweep::MaximumTuneMs) {
            s.abandoned = "Bode: GEN did not confirm its restore: check GEN";
            generator = true; // The generator's part ends here (its driver keeps the original as the request).
        }
    }
    const bool scope = data.scopeRequest().same(s.original) ||
        (data.scopeHeld() ? data.stageScope(s.original) : data.configureScope(s.original));
    if (!scope) refused = true;
    if (generator && scope) { s.touched = false; s.asked = false; s.attempts = 0; return true; }
    if (refused && ++s.attempts >= RetryLimit) { // Never a lock for ever.
        s.touched = false; s.asked = false; s.attempts = 0;
        s.abandoned = "Bode: restore refused: check GEN and the Scope";
    }
    s.retryAt = refused ? now + RetryMs : now; // A confirmation is polled every pass.
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
    return f == Sweep::Failure::Generator ? "Bode stopped: generator refused the frequency"
        : f == Sweep::Failure::Unconfirmed ? "Bode stopped: generator did not confirm the frequency" : "Bode stopped: capture refused";
}
} // namespace

bool ApplicationController::bodeRunning() const { return built && panel_ == Panel::Bode && session().sweep.running(); }
bool ApplicationController::bodeOwns() const { return built && (session().touched || session().sweep.running()); }
bool ApplicationController::bodeRestoring() const { return bodeOwns() && !session().sweep.running(); }
void ApplicationController::openBode(uint32_t now) {
    const auto &request = data_.scopeRequest();
    const char *why = state_.mode != MeasurementMode::Oscilloscope || remote_ ? "Bode needs the Scope (not Remote)"
        : data_.scopeHeld() ? "Press RUN first: Bode needs a running capture"
        : !request.frontend.enabled[0] || !request.frontend.enabled[1] ? "Enable CH1 and CH2 for Bode"
        : data_.scopeMetadata().layout == scope::Layout::InterleavedCh1 ? "Turn Interleave CH1 off for Bode"
        : data_.scopeRecording() || data_.zeroBusy() || autoBusy() ? "Finish the running operation first"
        : data_.generator().status() == DataStatus::HardwareError ? "Generator fault: switch it off and on"
        : data_.generator().busy() ? "Generator busy: try again when GEN is set" // A ZT706S frame on its way.
        : nullptr;
    if (why) { view_.notify(why, now); return; }
    auto &s = session();
    if (!restore(data_, s, now)) { view_.notify("Bode: the last sweep's restore is pending", now); return; } // Never two sessions' originals.
    if (!readClock(data_, s)) { view_.notify("Generator clock unknown: switch GEN on and off", now); return; }
    limit(s, data_.generator().settings());
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
    // Not whole yet: serviceBode polls a confirmation (a ZT706S acknowledge) or tries a refusal again.
    if (!restore(data_, session(), now)) notice = session().asked ? "Bode closed: restoring GEN" : "Bode: instrument busy, restoring";
    view_.bode().hide();
    closePanel();
    dataRefresh_ = true;
    if (notice) view_.notify(notice, now);
}
void ApplicationController::startBodeSweep(uint32_t now) {
    auto &s = session();
    if (s.saving || bodeCsvFile().busy()) { view_.notify("Bode: saving the CSV, the next sweep after it", now); return; }
    // The DAC clock and the generator's level (amplitude) as they are now (a host may have changed either
    // since the setup opened, e.g. over SCPI while the result showed): the limit, then the grid, from them.
    if (!readClock(data_, s)) { view_.notify("Generator clock unknown: switch GEN on and off", now); return; }
    limit(s, data_.generator().settings());
    const auto grid = scope::BodeGrid::of(s.settings, 1000, s.maximum);
    if (!grid.valid()) { view_.notify("Bode: Stop must be above Start", now); return; }
    if (!restore(data_, s, now)) { view_.notify("Bode: the last sweep's restore is pending", now); return; }
    // The originals only from a settled generator: a ZT706S frame still on its way (the user's, or a
    // recovery round) would make enabled() an intent, not the state the restore must return to.
    const auto &output = data_.generator();
    const auto original = confirmation(output, output.enabled());
    if (original == Confirmation::Pending) { view_.notify("Generator busy: try again when GEN is set", now); return; }
    if (original == Confirmation::Failed) { view_.notify("Generator state unconfirmed: set output in GEN", now); return; }
    s.original = data_.scopeRequest();
    s.generator = output.settings();
    s.generatorOn = output.enabled();
    const auto excitation = excitationOf(s, s.generator);
    if (const char *why = Drives::refusal(excitation)) { view_.notify(why, now); return; }
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
    // A confirmation still pending (the ZT706S module's acknowledge) is polled every pass, nothing sent.
    const bool retry = s.touched && !s.sweep.running() && int32_t(now - s.retryAt) >= 0 && shutdown_ == Shutdown::None;
    if (retry && restore(data_, s, now)) dataRefresh_ = true; // Whole: Run is offered again.
    if (s.abandoned) { view_.notify(s.abandoned, now); s.abandoned = nullptr; }
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
