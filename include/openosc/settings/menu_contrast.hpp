#pragma once
#include <cstdint>

namespace openosc {
// Settings > Display > Menu contrast (GitHub #44): how menus, panels and their lists are drawn.
// Theme keeps the colours of the theme and the Scope's look; High draws them black with white
// text (ui::MenuPalette). Instrument screens and colour samples are never affected.
enum class MenuContrast : uint8_t { Theme, High, Count };
inline constexpr unsigned MenuContrastCount = unsigned(MenuContrast::Count);
inline constexpr const char *MenuContrastKeys[] = {"theme", "high"};          // settings.json display.menu_contrast.
inline constexpr const char *MenuContrastNames[] = {"Theme", "High contrast"}; // The Settings row's options.
static_assert(sizeof MenuContrastKeys / sizeof *MenuContrastKeys == MenuContrastCount &&
              sizeof MenuContrastNames / sizeof *MenuContrastNames == MenuContrastCount);
} // namespace openosc
