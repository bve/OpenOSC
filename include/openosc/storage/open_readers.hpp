#pragma once

namespace openosc::storage {
// Files streamed over SCPI (MMEM:DATA?, any session): FF_FS_LOCK is 0, so deleting one is refused meanwhile.
inline unsigned openReaders = 0;
} // namespace openosc::storage
