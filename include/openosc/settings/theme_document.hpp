#pragma once
#include "openosc/settings/theme.hpp"
#include <ArduinoJson.h>

namespace openosc {
// ArduinoJson slots differ between the MCU and native 64-bit tests. The largest foreground JSON
// tree: JsonWorkspace (json_workspace.hpp) is sized by it.
inline constexpr size_t ThemeDocumentCapacity = JSON_OBJECT_SIZE(7) + JSON_OBJECT_SIZE(9)
    + InstrumentModeCount * (JSON_ARRAY_SIZE(ThemeInputElementLimit)
        + ThemeInputElementLimit * JSON_OBJECT_SIZE(10)) + 2048
    + JSON_OBJECT_SIZE(23)+JSON_ARRAY_SIZE(2)+2*JSON_OBJECT_SIZE(6)+JSON_OBJECT_SIZE(7)+JSON_OBJECT_SIZE(3)+512; // Plot: +counter.
using ThemeDocument = StaticJsonDocument<ThemeDocumentCapacity>;
}
