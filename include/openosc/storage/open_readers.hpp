#pragma once

namespace openosc::storage {
// Files streamed over SCPI (MMEM:DATA?, any session): FF_FS_LOCK is 0, so deleting one is refused meanwhile.
inline unsigned openReaders = 0, screenshotReaders = 0; // Every folder; Layout::Screenshots (the image browser's).
} // namespace openosc::storage
