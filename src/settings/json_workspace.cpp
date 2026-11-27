#include "openosc/settings/json_workspace.hpp"

namespace openosc {
namespace {
// SRAM4 (NOLOAD, not zeroed): a document or theme is constructed on every lease.
#ifdef OPENOSC_APPLICATION
#define JSON_WORKSPACE __attribute__((section(".settings_workspace")))
#else
#define JSON_WORKSPACE
#endif
alignas(std::max_align_t) uint8_t documentStorage[JsonWorkspace::DocumentBytes] JSON_WORKSPACE;
alignas(uint32_t) char textStorage[JsonWorkspace::TextCapacity] JSON_WORKSPACE;
constexpr unsigned ThemeSlots = unsigned(JsonWorkspace::ThemeSlot::Count);
alignas(Theme) uint8_t themeStorage[ThemeSlots][sizeof(Theme)] JSON_WORKSPACE;
// Foreground only (the main loop and its file operations); no interrupt handler parses JSON.
bool textLeased = false, documentLeased = false, themeLeased[ThemeSlots] = {};
void (*reclaimText)() = nullptr;
}
char *JsonWorkspace::acquireText() {
    if (textLeased) return nullptr;
    if (reclaimText) reclaimText();
    textLeased = true;
    return textStorage;
}
char *JsonWorkspace::borrowText(void (*reclaim)()) {
    if (textLeased || reclaimText) return nullptr;
    reclaimText = reclaim;
    return textStorage;
}
void JsonWorkspace::returnText() { reclaimText = nullptr; }
void JsonWorkspace::releaseText() { textLeased = false; }
void *JsonWorkspace::acquireDocument() {
    if (documentLeased) return nullptr;
    documentLeased = true;
    return documentStorage;
}
void JsonWorkspace::releaseDocument() { documentLeased = false; }
void *JsonWorkspace::acquireTheme(ThemeSlot slot) {
    const unsigned index = unsigned(slot);
    if (index >= ThemeSlots || themeLeased[index]) return nullptr;
    themeLeased[index] = true;
    return themeStorage[index];
}
void JsonWorkspace::releaseTheme(ThemeSlot slot) { themeLeased[unsigned(slot)] = false; }
bool JsonWorkspace::themeHeld(ThemeSlot slot) { return unsigned(slot) < ThemeSlots && themeLeased[unsigned(slot)]; }
bool JsonWorkspace::idle() {
    for (bool held : themeLeased) if (held) return false;
    return !textLeased && !documentLeased;
}
bool JsonWorkspace::textHeld() { return textLeased; }
bool JsonWorkspace::documentHeld() { return documentLeased; }
} // namespace openosc
