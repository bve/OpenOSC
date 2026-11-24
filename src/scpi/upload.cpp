#include "openosc/scpi/engine.hpp"
#include <cstdint>

// MMEMory:UPLoad (docs/SCPI_DESKTOP.md): a firmware image sent into /openosc/UPDATE in blocks.
//   MMEMory:UPLoad:DATA "<name>",<offset>,<block>   1 to 512 bytes at <offset> of <name>.TMP
//   MMEMory:UPLoad:END?                             closes and publishes it: <bytes>,<crc32>
// The block's bytes go into one of the SCPI stages as they come (uploadBegin, BlockSink), and its
// command writes them to the file (Backend::uploadData): FatFs runs in the foreground only.
namespace openosc::scpi {
namespace {
// The text in front of the block, '"name",offset,': a bare file name of 1..size-1 characters and a byte offset.
int uploadArguments(const char *text, char *name, size_t size, uint32_t &offset) {
    const auto skip = [](const char *p) { while (*p == ' ' || *p == '\t') ++p; return p; };
    const char *p = text;
    if (*p != '"' && *p != '\'') return *p ? -104 : -109;
    const char quote = *p++;
    size_t length = 0;
    while (*p && *p != quote) {
        if (*p == '/' || *p == '\\' || length + 1 >= size) return -224;
        name[length++] = *p++;
    }
    if (*p++ != quote || !length) return length ? -104 : -224;
    name[length] = 0;
    p = skip(p);
    if (*p++ != ',') return -109;
    p = skip(p);
    if (*p < '0' || *p > '9') return -104;
    uint64_t value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + unsigned(*p++ - '0');
        if (value > UINT32_MAX) return -222;
    }
    offset = uint32_t(value);
    p = skip(p);
    if (*p++ != ',') return -109;
    return *skip(p) ? -108 : 0;
}
} // namespace
int16_t Engine::uploadBegin(const Command &c, uint32_t bytes) {
    char name[Backend::UploadNameBytes];
    uint32_t offset = 0;
    if (c.query) return -108;
    if (const int code = uploadArguments(c.argument, name, sizeof name, offset)) return int16_t(code);
    if (!bytes) return -222;
    if (bytes > StageBytes) return -223;
    if (!(upload_ = claimStage(&upload_))) return -200; // Both stages are in use (an output stream, the other session's block).
    uploadAt_ = 0;
    return 0;
}
bool Engine::uploadCommand(const Command &c) {
    if (Header::matches(c.header, "MMEMory:UPLoad:DATA")) {
        if (!c.block) { error(c.query ? -108 : -109, c.query); return true; } // Its data is a block (uploadBegin).
        char name[Backend::UploadNameBytes];
        uint32_t offset = 0;
        uploadArguments(c.argument, name, sizeof name, offset); // Checked by uploadBegin.
        const int code = backend_.uploadData(name, offset, upload_, uploadAt_);
        endUpload();
        if (code) error(code);
    } else if (Header::matches(c.header, "MMEMory:UPLoad:END")) {
        uint32_t bytes = 0, crc = 0;
        int code = !c.query || *c.argument ? -108 : backend_.uploadEnd(bytes, crc);
        if (code) error(code, c.query);
        else reply("%lu,%lu", static_cast<unsigned long>(bytes), static_cast<unsigned long>(crc));
    } else return false;
    return true;
}
} // namespace openosc::scpi
