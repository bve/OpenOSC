#pragma once

namespace openosc::storage {
// FatFs does not lock open files. These counts cover SCPI readers at the deletion entry points:
// MMEM:DEL and the Settings catalogs (screenshots, waveforms). Recorders and exporters write .TMP
// files and rename them only to unused final names, so they never replace a file these readers can open.
inline unsigned openReaders = 0, screenshotReaders = 0, captureReaders = 0;
} // namespace openosc::storage
