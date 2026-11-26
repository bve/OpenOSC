#pragma once
#include "openosc/data/file_catalog.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/storage/open_readers.hpp"
#include <cstring>
#include <string>
#include <strings.h>

namespace openosc {
// Settings > Files & USB > Waveforms and Clear waveforms (GitHub #186): the Analyze waveform exports in
// /openosc/CAPTURES (FileCatalog). Only names ending in .CSV, .RAW, .CSV.GZ or .RAW.GZ (any case, with
// a name before the extension) are listed and deleted: plain exports of earlier firmware included, the
// recorder's .TMP and every other file or directory kept. The controller owns one only while its list,
// its confirmation or its clear is open (from the LVGL heap: no DTCM, no acquisition memory).
class CaptureFiles final : public FileCatalog {
public:
    static constexpr const char *Directory = storage::Layout::Captures;
    static_assert(std::char_traits<char>::length(Directory) < DirectoryBytes);
    static bool supportedName(const char *name) {
        static constexpr const char *Extensions[] = {".CSV.GZ", ".RAW.GZ", ".CSV", ".RAW"};
        const size_t length = std::strlen(name);
        if (length >= NameBytes) return false;
        for (const char *extension : Extensions) {
            const size_t bytes = std::strlen(extension);
            if (length > bytes && !strcasecmp(name + length - bytes, extension)) return true;
        }
        return false;
    }
    explicit CaptureFiles(Filesystem &fs) : FileCatalog(fs, Captures) {}
    // A row of the page: its file's size ("512 B", "1234 KiB", KiB rounded up), valid until the next call for that row.
    const char *size(unsigned row) const;

private:
    static constexpr Profile Captures = {
        Directory, &supportedName, &storage::captureReaders,
        {"Cannot read waveform directory", "Waveform directory read failed", "Cannot delete waveform", "Waveform in use over USB",
         "Waveform cleanup already in progress", "Waveform cleanup stopped; in use over USB",
         "Waveform cleanup stopped; delete failed", "Waveform cleanup stopped; read failed"}};
    mutable char sizes_[PageSize][16]{};
};
} // namespace openosc
