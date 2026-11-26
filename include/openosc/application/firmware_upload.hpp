#pragma once
#include "openosc/storage/filesystem.hpp"
#include "openosc/update/crc32.hpp"

namespace openosc {
// MMEMory:UPLoad (SCPI): a firmware image written in chunks into /openosc/UPDATE/<name>.TMP, published
// under <name> by end() only for a whole file. One upload at a time, owned by the SCPI session that
// started it (an opaque pointer). That session's end (detach(): a USB reset or a DTR drop, which may
// run inside another file operation's wait) touches no file: service() in the main loop, and the next
// write or end, then close the file and remove its .TMP. A shutdown cancels it directly (cancel()).
// write() and end() run in the foreground dispatcher. The int results are 0 or the SCPI error to report.
class FirmwareUpload {
public:
    static constexpr size_t NameBytes = 48; // With the terminator: names of 1..47 characters.
    static constexpr size_t PathBytes = NameBytes + 24; // "/openosc/UPDATE/<name>.TMP" or ".OLD" with room.
    explicit FirmwareUpload(Filesystem &fs) : fs_(fs) {}
    static bool path(const char *name, char *out, size_t size); // "/openosc/UPDATE/<name>"; false: it does not fit.
    // Offset 0 starts the upload (replacing a stale .TMP; another session's open upload: -221); any other
    // offset must continue the open upload of the same name where it ended, else it is cancelled (-222).
    int write(const void *owner, const char *name, uint32_t offset, const uint8_t *data, size_t size);
    // Closes and checks the file as Recovery would before flashing it (-230 when it is not a whole image of
    // this board; the .TMP is removed and an existing <name> stays), replaces <name> and reports the bytes
    // written and their CRC-32. A replacement that fails (-200) keeps the existing <name>, at worst as
    // <name>.OLD.
    int end(const void *owner, uint32_t &bytes, uint32_t &crc);
    bool open() const { return open_; }
    void cancel();
    // The owner's session ended: no file access here, service() cleans up.
    void detach(const void *owner) { if (open_ && owner_ == owner) { owner_ = nullptr; abandoned_ = true; } }
    void service() { if (abandoned_) cancel(); } // Main loop: closes and removes a detached upload.
private:
    static int check(); // temporary_ against its final_ name: 0, -230 (not an image) or -200.
    Filesystem &fs_;
    // The file and the names are static (.bss): the controller holding this object is a local of main(),
    // so its members take stack that the linker's 48 KiB check cannot count. One upload at a time anyway.
    static FIL file_;
    bool open_ = false, abandoned_ = false; // abandoned_: its session ended, the file is still open.
    const void *owner_ = nullptr;
    uint32_t written_ = 0;
    Crc32 crc_;
    static char name_[NameBytes], temporary_[PathBytes], final_[PathBytes], aside_[PathBytes];
};
} // namespace openosc
