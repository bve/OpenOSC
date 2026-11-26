#include "openosc/application/controller.hpp"
#include "openosc/application/decode_csv_file.hpp"
#include "openosc/decode/table_text.hpp"
#include <cstdio>

// The decoded result to /openosc/DECODE (plan §9 stage 7): the menu's Save CSV and DECode:SAVE. The
// export holds the published result (its record named in the toast: what is saved is what was shown)
// and the file is written in slices of the main loop (serviceDecoderCsv).
namespace openosc {
int16_t ApplicationController::saveDecoderCsv(uint32_t now) {
    auto &file = decodeCsvFile();
    auto &csv = decoding::workspaceExport();
    // No new file once a power off, restart or Recovery is under way (or a Recovery is queued): the
    // shutdown ended the writer in its prepare() and unmounts next; USB answers until it is stopped.
    if (shutdown_ != Shutdown::None || shutdownSequence_.active() || recoveryPending_) return -200;
    if (file.busy()) return -200;
    const auto *result = decoder_.results().published();
    const uint32_t ticket = csv.begin(decoding::CsvExport::Owner::File, decoder_.results(), decoder_.settings(), "OpenOSC " OPENOSC_VERSION);
    if (!ticket) {
        const auto refusal = csv.refusal();
        if (uiReady_) {
            // Why nothing was saved: the result's own state (the table's empty line), never a storage word.
            char text[96];
            if (refusal == decoding::CsvExport::Refusal::Busy) std::snprintf(text, sizeof text, "Decoded CSV busy: an export runs");
            else if (refusal == decoding::CsvExport::Refusal::Format) std::snprintf(text, sizeof text, "Decoded result unreadable: not saved");
            else if (!prefs_.scope.decoder.enabled) std::snprintf(text, sizeof text, "Decoder off: no decode to save");
            else std::snprintf(text, sizeof text, "No complete decode to save\n%s", decoding::TableText::empty(result, decoder_.running()));
            view_.notify(Tooltip::Unavailable, text, now);
        }
        return refusal == decoding::CsvExport::Refusal::Busy ? -200 : -230;
    }
    if (!file.start(csv, ticket, fs_)) { // It ended the export.
        if (uiReady_) view_.notify(Tooltip::Critical, file.error(), now);
        return -200;
    }
    if (uiReady_) {
        char text[72];
        std::snprintf(text, sizeof text, "Saving CSV: %s %u ev, record %lu", decoding::Protocols::name(result->protocol()), result->count(),
                      static_cast<unsigned long>(result->key().sequence));
        view_.notify(Tooltip::Progress, text, now);
    }
    return 0;
}
void ApplicationController::serviceDecoderCsv(uint32_t now) {
    auto &file = decodeCsvFile();
    if (!file.busy()) return;
    // A shutdown never shares the volume with a writer (prepare() has ended it; this keeps it so).
    if (shutdownSequence_.active()) { file.cancel("DECODE CSV CANCELLED: SHUTDOWN"); return; }
    file.service();
    if (!file.busy() && uiReady_) view_.notify(file.succeeded() ? Tooltip::Done : Tooltip::Critical, file.succeeded() ? file.path() : file.error(), now);
}
const char *ApplicationController::decoderCsvSaved() const {
    const auto &file = decodeCsvFile();
    return file.busy() ? "BUSY" : file.succeeded() ? file.path() : file.error() ? file.error() : "";
}
} // namespace openosc
