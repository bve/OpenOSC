#pragma once
#include "openosc/data/file_catalog.hpp"
#include "openosc/storage/layout.hpp"
#include "openosc/storage/open_readers.hpp"
#include <string>
#include <strings.h>

namespace openosc::screenshot {
// Settings > Files & USB > Screenshots: the PNG and BMP files of /openosc/SCREENSHOTS (FileCatalog).
class ImageCatalog final : public FileCatalog {
public:
    static constexpr const char *Directory = storage::Layout::Screenshots;
    static_assert(std::char_traits<char>::length(Directory) < DirectoryBytes);
    static bool supportedName(const char *name) {
        const size_t length = std::char_traits<char>::length(name);
        if (length < 5 || length >= NameBytes) return false;
        const char *extension = name + length - 4;
        return !strcasecmp(extension, ".PNG") || !strcasecmp(extension, ".BMP");
    }
    explicit ImageCatalog(Filesystem &fs) : FileCatalog(fs, Images) {}

private:
    static constexpr Profile Images = {
        Directory, &supportedName, &storage::screenshotReaders,
        {"Cannot read screenshot directory", "Screenshot directory read failed", "Cannot delete image", "Image in use over USB",
         "Image cleanup already in progress", "Image cleanup stopped; in use over USB", "Image cleanup stopped; delete failed",
         "Image cleanup stopped; read failed"}};
};
} // namespace openosc::screenshot
