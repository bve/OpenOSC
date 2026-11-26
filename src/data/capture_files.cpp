#include "openosc/data/capture_files.hpp"
#include <cstdio>

namespace openosc {

const char *CaptureFiles::size(unsigned row) const {
    if (row >= PageSize) return "";
    const unsigned long bytes = entry(row).bytes;
    if (bytes < 1024) std::snprintf(sizes_[row], sizeof sizes_[row], "%lu B", bytes);
    else std::snprintf(sizes_[row], sizeof sizes_[row], "%lu KiB", (bytes + 1023) / 1024);
    return sizes_[row];
}
} // namespace openosc
