#pragma once
#include "openosc/storage/filesystem.hpp"
#include <cstddef>
#include <cstdint>

namespace openosc {
// A page of one owned directory's files (Settings > Files & USB: the screenshots, the waveforms), the
// selected file's deletion and a clear of every listed file, one file per foreground turn. Only plain
// files of that directory whose names the owner accepts are listed or deleted: no subdirectory, hidden
// or system entry, no wildcard and no path from outside. Every path is the owner's directory, '/', and
// a name f_readdir returned. No file is held open while the page shows. While USB readers of these
// files are counted (MMEM:DATA?), nothing is deleted.
class FileCatalog {
public:
    static constexpr unsigned PageSize = 5, NameBytes = 256, DirectoryBytes = 32;
    // The owner's directory (shorter than DirectoryBytes), '/', a name of up to NameBytes-1 characters and the terminator.
    static constexpr unsigned PathBytes = DirectoryBytes + 1 + NameBytes;
    // What the owner's notices say, by failure.
    struct Messages {
        const char *open;          // The directory cannot be opened.
        const char *read;          // Reading the directory failed part way.
        const char *erase;         // The selected file was not deleted.
        const char *inUse;         // A USB reader holds one of these files.
        const char *clearing;      // A clear is already running.
        const char *clearInUse;    // A clear stopped: a USB reader arrived.
        const char *clearErase;    // A clear stopped: a file was not deleted.
        const char *clearRead;     // A clear stopped: reading the directory failed.
    };
    struct Profile {
        const char *directory;
        bool (*accepts)(const char *name); // The owner's file names.
        const unsigned *readers;           // USB readers of these files: no deletion while any.
        Messages messages;
    };
    struct Entry { char name[NameBytes]{}; uint32_t bytes = 0; };
    FileCatalog(Filesystem &fs, const Profile &profile) : fs_(fs), profile_(profile) {}
    ~FileCatalog() { cancelClear(); }
    FileCatalog(const FileCatalog &) = delete;
    FileCatalog &operator=(const FileCatalog &) = delete;
    const char *directory() const { return profile_.directory; }
    bool refresh();
    bool move(int direction); // Wraps.
    bool eraseSelected();
    bool beginClear();
    bool clearStep(); // One file per foreground turn; stops on the first error.
    void cancelClear();
    bool clearing() const { return clearing_; }
    unsigned erased() const { return erased_; }
    unsigned count() const { return count_; }
    unsigned selected() const { return selected_; }
    unsigned first() const { return first_; }
    unsigned visible() const { return visible_; }
    // The last listing read the whole directory (a missing one is an empty list). False: error()
    // says why and the page is empty, never a part of the directory.
    bool listed() const { return listed_; }
    const Entry &entry(unsigned row) const { return entries_[row < PageSize ? row : 0]; }
    const char *name() const { return count_ && selected_ - first_ < visible_ ? entries_[selected_ - first_].name : ""; }
    bool path(char *output, size_t capacity) const;
    const char *error() const { return error_; }

private:
    bool listable(const FILINFO &info) const;
    bool scan(unsigned first);
    bool fail(const char *message) { error_ = message; return false; }
    bool inUse() const { return profile_.readers && *profile_.readers; }
    Filesystem &fs_;
    const Profile &profile_;
    Entry entries_[PageSize];
    unsigned count_ = 0, selected_ = 0, first_ = 0, visible_ = 0, erased_ = 0;
    DIR directory_{};
    bool clearing_ = false, listed_ = false;
    const char *error_ = nullptr;
};
} // namespace openosc
