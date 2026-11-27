#if defined(__arm__)
#include "openosc/companion/link.hpp"
#include "openosc/platform/board.hpp"
#include "ff.h"
#include <time.h>

extern "C" DWORD get_fattime() {
    using namespace openosc;
    const time_t seconds = companion::link().time(Board::milliseconds());
    struct tm utc{};
    if (seconds > 0 && gmtime_r(&seconds, &utc) && utc.tm_year >= 80 && utc.tm_year <= 207)
        return uint32_t(utc.tm_year - 80) << 25 | uint32_t(utc.tm_mon + 1) << 21 |
               uint32_t(utc.tm_mday) << 16 | uint32_t(utc.tm_hour) << 11 |
               uint32_t(utc.tm_min) << 5 | uint32_t(utc.tm_sec / 2);
    return uint32_t(FF_NORTC_YEAR - 1980) << 25 | uint32_t(FF_NORTC_MON) << 21 | uint32_t(FF_NORTC_MDAY) << 16;
}
#endif
