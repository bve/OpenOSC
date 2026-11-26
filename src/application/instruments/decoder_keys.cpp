#include "openosc/application/controller.hpp"
#include "openosc/platform/input.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

// The protocol decoder on the Scope (docs/SCOPE_PROTOCOL_DECODERS_PLAN.md §4.4): its settings
// panel, the keys of its own screens (Split, Decoder only) and what the view shows of the job.
namespace openosc {
bool ApplicationController::decoderScreen() const {
    return uiReady_ && state_.mode == MeasurementMode::Oscilloscope && !state_.settingsOpen && panel_ == Panel::None &&
           prefs_.scope.decoder.enabled && prefs_.scope.decoder.view != decoding::View::Annotations;
}
// UART autobaud (plan §6.3), a hint only. MENU on Detect baud in HOLD: the held record is searched in
// the job's bounded slices (decoding waits). A confirmed rate (one clean rate, >= 3 correct characters,
// at most one bad per eight) is shown as "rate? MENU" and applied only by that next MENU, and only
// while the settings are those it was found with; anything else is said, never applied.
bool ApplicationController::baudHintReady() const {
    const auto &search = decoder_.baudSearch();
    return search.state == decoding::DecodeJob::BaudSearch::State::Done && search.hint.confirmed &&
           search.settingsHash == decoder_.settings().hash();
}
void ApplicationController::baudAction(uint32_t now) {
    const auto &search = decoder_.baudSearch();
    if (baudHintReady()) {
        auto settings = prefs_.scope.decoder;
        settings.uart.baud = search.hint.baud;
        const unsigned valid = search.hint.valid, invalid = search.hint.invalid;
        applyDecoder(settings, now); // A new hash: the hint is stale now (never applied twice).
        char text[64];
        const char *mode = settings.protocol == decoding::Protocol::Rs232 ? "RS-232" : "UART";
        std::snprintf(text, sizeof text, "%s %lu bd applied (%u ok, %u bad)", mode, static_cast<unsigned long>(settings.uart.baud), valid, invalid);
        view_.notify(Tooltip::Done, text, now);
        return;
    }
    if (decoder_.running()) { view_.notify(Tooltip::Unavailable, "Detect baud: decoder busy", now); return; }
    scope::FrameLease held;
    if (!data_.scopeHeld() || !data_.acquireScope(held)) { view_.notify(Tooltip::Unavailable, "Detect baud needs HOLD (the record to measure)", now); return; }
    if (!decoder_.searchBaud(held)) view_.notify(Tooltip::Unavailable, "Detect baud: check the source and scale", now);
}
void ApplicationController::baudHintText(char *out, size_t capacity) const {
    using State = decoding::DecodeJob::BaudSearch::State;
    const auto &search = decoder_.baudSearch();
    switch (search.state) {
    case State::Running: std::snprintf(out, capacity, "measuring %u%%", decoder_.searchPercent()); return;
    case State::Cancelled: std::snprintf(out, capacity, "cancelled"); return;
    case State::Done:
        if (baudHintReady()) { std::snprintf(out, capacity, "%lu? MENU", static_cast<unsigned long>(search.hint.baud)); return; }
        if (search.settingsHash == decoder_.settings().hash()) { std::snprintf(out, capacity, "no sure rate"); return; }
        break;
    case State::Idle: break;
    }
    std::snprintf(out, capacity, data_.scopeHeld() ? "MENU: measure" : "HOLD first");
}
// Save CSV's value: the published result it would save.
void ApplicationController::decoderSummary() {
    const auto *shown = prefs_.scope.decoder.enabled ? decoder_.results().published() : nullptr;
    char text[24] = "no result";
    if (shown && shown->status == decoding::DecodeStatus::Done)
        std::snprintf(text, sizeof text, "%u ev #%lu", shown->count(), static_cast<unsigned long>(shown->key().sequence));
    decoderEditor_.result(text);
    baudHintText(text, sizeof text);
    decoderEditor_.baudHint(text);
}
void ApplicationController::openDecoder(unsigned row) {
    decoderEditor_ = ui::DecoderProperties(prefs_.scope.decoder);
    decoderSummary();
    openPanel(Panel::Decoder, decoderEditor_, row);
}
void ApplicationController::applyDecoder(const decoding::Settings &settings, uint32_t now) {
    if (!settings.valid() || settings == prefs_.scope.decoder) return;
    if (settings.enabled && !prefs_.scope.decoder.enabled) decoderRow_ = 0;
    prefs_.scope.decoder = settings;
    configureDecoder(); // Another protocol, source or threshold: the old result is gone.
    changed(now);
    dataRefresh_ = true;
}
// Out of line: its buffers stay out of panelKeys' frame (on the deepest run() chain, test_stack_budget).
__attribute__((noinline)) void ApplicationController::decoderPanelKeys(uint32_t keys, uint32_t now) {
    const auto key = fieldKeys(keys, decoderEditor_, now);
    if (key == FieldKey::Action) { // Save CSV: the result its value named. Detect baud: search, or apply.
        const auto row = decoderEditor_.row(navigator_.row());
        if (row == ui::DecoderProperties::Row::SaveCsv) saveDecoderCsv(now);
        if (row == ui::DecoderProperties::Row::DetectBaud) baudAction(now);
        decoderSummary();
        view_.properties(decoderEditor_, navigator_);
        return;
    }
    if (key != FieldKey::Committed) return;
    applyDecoder(decoderEditor_.settings(), now);
    // The editor stays (its LIN ID cursor too) unless the settings in force differ from it.
    if (decoderEditor_.settings() != prefs_.scope.decoder) decoderEditor_ = ui::DecoderProperties(prefs_.scope.decoder);
    decoderSummary();
    view_.properties(decoderEditor_, navigator_);
}
// Plan §4.4. F1 Protocol and F2 View open the panel on their row; F3 Jump; F4 leaves the decoder (the
// job stops and lets its record go, the settings and the last view stay saved), or Detail; MENU opens
// the selected event's Detail and closes it again; Up/Down choose an event (Detail: scroll), Left/Right
// a page (Detail: the event before or after); HOLD as on the Scope.
// Out of line: its buffers stay out of panelKeys' frame (on the deepest run() chain, test_stack_budget).
__attribute__((noinline)) void ApplicationController::decoderKeys(uint32_t keys, uint32_t now) {
    if (holdKeys(keys, now)) return;
    if (keys & Keypad::F1) { decoderDetail_ = false; openDecoder(ui::DecoderProperties(prefs_.scope.decoder).index(ui::DecoderProperties::Row::Protocol)); return; }
    if (keys & Keypad::F2) { decoderDetail_ = false; openDecoder(ui::DecoderProperties(prefs_.scope.decoder).index(ui::DecoderProperties::Row::View)); return; }
    if (keys & Keypad::F3) { jumpToEvent(now); return; }
    if (decoderDetail_ && (keys & (Keypad::F4 | Keypad::Menu))) { decoderDetail_ = false; return; } // Back to the table only.
    if (keys & Keypad::F4) {
        auto settings = prefs_.scope.decoder;
        settings.enabled = 0;
        applyDecoder(settings, now);
        return;
    }
    const auto *shown = decoderResult();
    const unsigned count = shown ? shown->count() : 0, page = view_.decodeTable().rows();
    if (!count) return;
    unsigned row = decoderRow_ < count ? decoderRow_ : count - 1;
    if (keys & Keypad::Menu) { decoderDetail_ = true; decoderLine_ = 0; return; }
    if (decoderDetail_) {
        if (keys & Keypad::Up) decoderLine_ = uint16_t(decoderLine_ ? decoderLine_ - 1 : 0);
        if (keys & Keypad::Down) {
            decoding::TableText::Line line; // Scrolled while a page more is there.
            if (decoding::TableText::detail(*shown, row, prefs_.scope.decoder, decoderLine_ + page, line)) ++decoderLine_;
        }
        if (keys & (Keypad::Left | Keypad::Right)) {
            row = keys & Keypad::Left ? (row ? row - 1 : 0) : (row + 1 < count ? row + 1 : count - 1);
            decoderLine_ = 0;
        }
        decoderRow_ = uint16_t(row);
        return;
    }
    if (keys & Keypad::Up) row = row ? row - 1 : 0;
    if (keys & Keypad::Down) row = row + 1 < count ? row + 1 : count - 1;
    if (keys & Keypad::Left) row = row > page ? row - page : 0;
    if (keys & Keypad::Right) row = row + page < count ? row + page : count - 1;
    decoderRow_ = uint16_t(row);
}
// F3 Jump (plan §5.5): the trace at the selected event, only on the very record it was decoded from:
// held (HOLD) and with the result's full key. A new analog record is never taken for it: RAW UNAVAILABLE.
// Decoder only shows Split for it (the table stays).
void ApplicationController::jumpToEvent(uint32_t now) {
    const auto *shown = decoderResult();
    if (!shown || decoderRow_ >= shown->count()) return;
    scope::FrameLease held;
    if (!data_.scopeHeld() || !data_.acquireScope(held) || !decoder_.shows(held.view().metadata())) {
        view_.notify(Tooltip::Unavailable, "RAW UNAVAILABLE: Jump needs the decoded record held", now);
        return;
    }
    const auto &m = held.view().metadata();
    const auto &e = shown->event(decoderRow_);
    auto options = view_.waveform().options();
    // The window without pan, then the pan (ppb of the record) that centres the event in it.
    plot::Viewport base;
    auto zero = options; zero.panPpb = 0;
    const auto &shownViewport = view_.waveform().model().viewport();
    const unsigned width = shownViewport.width > 1 ? shownViewport.width : unsigned(plot::PlotFrame::PlotWidth);
    if (m.samples < 2 || !base.calculate(m, zero, width, shownViewport.height ? shownViewport.height : 1)) return;
    const int64_t centre = (int64_t(e.first) + int64_t(e.last)) * 32768; // Q16 of the middle point.
    const int64_t shift = centre - int64_t(base.spanQ16 / 2) - base.startQ16;
    const double pan = std::round(double(shift) * plot::Options::PanFull / (double(m.samples - 1) * 65536));
    held.release();
    if (prefs_.scope.decoder.view == decoding::View::Table) {
        auto settings = prefs_.scope.decoder;
        settings.view = decoding::View::Split;
        applyDecoder(settings, now);
    }
    beginHeldView(); // HOLD exploration: RUN brings the view back.
    options.panPpb = int32_t(std::clamp<double>(pan, -plot::Options::PanFull, plot::Options::PanFull));
    view_.waveform().configure(options);
    decoderDetail_ = false;
    dataRefresh_ = true;
}
void ApplicationController::syncHostRecord(uint32_t now) {
    auto &hosts = data_.frameHosts();
    decoder_.hostDemand(hosts.demanding(now));
    if (const auto *shared = decoder_.hostRecord())
        if (!hosts.owed(shared->metadata().sequence, decoder_.hostSharedAt(), now)) decoder_.dropHostRecord();
}
void ApplicationController::configureDecoder() {
    decoder_.configure(prefs_.scope.decoder);
    data_.attachBackground(prefs_.scope.decoder.enabled ? &decoder_ : nullptr);
}
// Plan §5.3, RUN Complete and HOLD under a drawn trace: the trace follows the job. A new record goes
// to the job first and is shown when its result is published (handOver), so the trace, its
// annotations and the table are one record; records that come while one is read are not shown
// (counted as skipped). A record decoded before (HOLD), or one the job does not take (a status
// at once), is shown as it is. Off: the newest record, as always.
bool ApplicationController::scopeFrame(scope::FrameLease &frame) {
    if (!prefs_.scope.decoder.enabled) return data_.acquireScope(frame);
    decoder_.keepForDisplay(true);
    if (decoder_.handOver(frame)) return true;
    if (decoder_.running()) { noteSkipped(); return false; }
    if (!data_.acquireScope(frame)) return false;
    return !decoder_.offer(frame); // Taken: shown when read through.
}
void ApplicationController::feedDecoder() {
    decoder_.keepForDisplay(false);
    if (decoder_.running()) { noteSkipped(); return; }
    scope::FrameLease frame;
    if (data_.acquireScope(frame)) decoder_.offer(frame);
}
void ApplicationController::noteSkipped() {
    scope::FrameLease newer;
    if (data_.acquireScope(newer, false)) decoder_.offer(newer); // Counted once per record, never taken.
}
// Under a trace only the result of the record it shows; without one the newest result.
const decoding::ResultSet *ApplicationController::decoderResult() const {
    if (!view_.traceDrawn()) return decoder_.results().published();
    const auto &model = view_.waveform().model();
    return model.hasFrame() ? decoder_.shows(model.metadata()) : nullptr;
}
void ApplicationController::showDecoder(uint32_t now) {
    const auto &settings = prefs_.scope.decoder;
    if (panel_ == Panel::Decoder && settings.enabled) { // The panel's live values (search progress, result).
        char hint[24];
        baudHintText(hint, sizeof hint);
        if (std::strcmp(hint, decoderEditor_.baudHint())) { decoderSummary(); view_.properties(decoderEditor_, navigator_); }
    }
    if (!settings.enabled) { // Off: the view is told once (F4, Protocol Off), then nothing a pass.
        if (view_.decoderShown()) view_.decoder(false, settings.view);
        decoderDetail_ = false;
        return;
    }
    view_.decoder(true, settings.view);
    const auto &results = decoder_.results();
    decoderRate_.update(results.revision(), results.published() != nullptr, now);
    const auto *shown = decoderResult();
    const unsigned count = shown ? shown->count() : 0;
    if (decoderRow_ >= count) decoderRow_ = uint16_t(count ? count - 1 : 0);
    if (!count) decoderDetail_ = false;
    decoding::TableText::Counters counters;
    counters.recordsPerSecondX10 = decoderRate_.perSecondX10();
    counters.age = decoderRate_.age(now, counters.ageMs);
    counters.skipped = decoder_.skipped();
    counters.exporting = decoder_.results().held(); // The decoder waits for an export (EXPORT).
    decoding::TableText::Line info;
    decoding::TableText::info(decoder_.settings(), shown, decoder_.decoding(), counters, info);
    ui::DecodeTable::Content table;
    table.set = shown; table.sets = &results; table.revision = results.revision(); table.selected = decoderRow_;
    table.empty = decoding::TableText::empty(shown, decoder_.running());
    table.settings = &decoder_.settings(); table.detail = decoderDetail_; table.detailLine = decoderLine_;
    view_.decoderResults(table, info, view_.traceDrawn() ? shown : nullptr);
}
} // namespace openosc
