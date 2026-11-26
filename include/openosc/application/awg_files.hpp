#pragma once
#include "openosc/generator/arbitrary.hpp"
#include "openosc/storage/filesystem.hpp"
#include "openosc/ui/images/browser.hpp"

namespace openosc {
// GEN > Waveform > AWG file (both boards, GitHub #175, docs/AWG.md): the waveform files in /openosc/AWG
// (*.CSV, a page of names at a time, as the screenshots' list) and the chosen one read in slices
// (SliceBytes a main-loop pass) through ArbitraryCsv into the generator's stage. No file is held while
// the list shows. The controller owns one only while the list is open (from the LVGL heap: no DTCM,
// no acquisition memory) and gives the codes their stage (generator::Output::beginArbitrary).
class AwgFiles {
public:
    static constexpr unsigned PageSize = ui::ImageList::Rows, NameBytes = 64;
    static constexpr uint32_t SliceBytes = 2048; // A pass's share: well under a millisecond of NOR reading.
    explicit AwgFiles(Filesystem &fs) : fs_(fs) {}
    ~AwgFiles() { close(); }
    AwgFiles(const AwgFiles &) = delete;
    AwgFiles &operator=(const AwgFiles &) = delete;
    static bool supportedName(const char *name);
    bool refresh();            // The page with the selection (the directory made when missing). False: error().
    bool move(int direction);  // Wraps, as the screenshots' list.
    ui::ImageList list() const;
    const char *name() const { return count_ && selected_ - first_ < visible_ ? names_[selected_ - first_] : ""; }
    unsigned count() const { return count_; }
    // The selected file opened for reading (false: error()); then read() a slice at a time into `sink`
    // until it is done (true) or failed (false, error()); points() the codes it held.
    bool open();
    enum class Step : uint8_t { More, Done, Failed };
    Step read(generator::CodeSink &sink, uint32_t budget = SliceBytes);
    void close();
    bool reading() const { return open_; }
    unsigned points() const { return csv_.points(); }
    const char *error() const { return error_; }
private:
    bool fail(const char *message) { error_ = message; return false; }
    bool scan(unsigned first);
    Filesystem &fs_;
    char names_[PageSize][NameBytes]{};
    uint32_t sizes_[PageSize]{};
    mutable char values_[PageSize][16]{};
    unsigned count_ = 0, selected_ = 0, first_ = 0, visible_ = 0;
    FIL file_{};
    bool open_ = false;
    generator::ArbitraryCsv csv_;
    char chunk_[512];
    char message_[64]{}; // "Line 12: code above 4095".
    const char *error_ = nullptr;
};
} // namespace openosc
