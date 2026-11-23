#include "openosc/scpi/engine.hpp"
#include "openosc/storage/layout.hpp"
#include <cstdio>
#include <cstring>
#include <strings.h>

namespace openosc::scpi {
namespace {
constexpr const char *Folders[] = {"SCREENSHOTS", "LOGS", "CAPTURES", "DECODE", "FRA"}; // Under Layout::Root.
// The next field up to ',' (quotes stripped); p stops at the comma or the end.
bool field(const char *&p, char *out, size_t size) {
    const char quote = *p == '"' || *p == '\'' ? *p++ : 0;
    size_t n = 0;
    while (*p && (quote ? *p != quote : *p != ',')) { if (n + 1 >= size) return false; out[n++] = *p++; }
    if (quote) { if (*p != quote) return false; ++p; }
    else while (n && out[n - 1] == ' ') --n;
    out[n] = 0;
    while (*p == ' ') ++p;
    return true;
}
// "FOLDER" (name: false) or "FOLDER/NAME" as the full path: a data folder and a finished file's plain name.
int path(const char *&p, bool name, char *out, size_t size) {
    char text[160];
    if (!field(p, text, sizeof text)) return -257;
    char *slash = std::strchr(text, '/');
    if (name != (slash != nullptr)) return -257;
    if (slash) *slash = 0;
    const char *folder = nullptr;
    for (const char *f : Folders) if (!strcasecmp(text, f)) folder = f;
    if (!folder) return -257;
    if (slash) {
        const char *file = slash + 1, *dot = std::strrchr(file, '.');
        const char last = *file ? file[std::strlen(file) - 1] : 0; // FatFs drops trailing dots and spaces: "X.TMP." is X.TMP.
        if (!*file || *file == '.' || last == '.' || last == ' ' || std::strpbrk(file, "/\\:*?\"<>|") || (dot && !strcasecmp(dot, ".TMP"))) return -257;
        const int n = std::snprintf(out, size, "%s/%s/%s", storage::Layout::Root, folder, file);
        return n > 0 && size_t(n) < size ? 0 : -257;
    }
    const int n = std::snprintf(out, size, "%s/%s", storage::Layout::Root, folder);
    return n > 0 && size_t(n) < size ? 0 : -257;
}
} // namespace
bool Engine::systemCommand(const Command &c) {
    const auto match = [&](const char *pattern) { return Header::matches(c.header, pattern); };
    const bool power = match("SYSTem:POWer:TIMeout"), timeout = match("SYSTem:DISPlay:TIMeout"), dim = match("SYSTem:DISPlay:DIM");
    if (power || timeout || dim) { // Minutes (0, 15, 30, 60), seconds (0, 30, 60, 120), percent (0, 5..25 by 5); 0: off.
        IdleSettings s;
        if (!backend_.idleSettings(s)) { error(-241, c.query); return true; }
        auto &value = power ? s.powerOffMinutes : timeout ? s.backlightSeconds : s.dimPercent;
        if (c.query) { reply("%lu", (unsigned long)value); return true; }
        double number;
        if (!argument(c, number)) return true;
        bool offered = false;
        if (power) for (uint32_t m : IdleSettings::OffChoices) offered |= number == m;
        else if (dim) for (uint32_t m : IdleSettings::DimLevels) offered |= number == m;
        else offered = number == 0 || number == 30 || number == 60 || number == 120;
        if (!offered) { error(-222); return true; }
        value = uint32_t(number);
        if (const int e = backend_.setIdle(s)) error(int16_t(e));
    } else if (match("MMEMory:CATalog")) { // "FOLDER"[,start]: total,count then count of "name",bytes.
        const char *p = c.argument;
        char folder[64], list[sizeof response_ - 32];
        double start = 0;
        if (!c.query) { error(-109); return true; }
        if (const int e = path(p, false, folder, sizeof folder)) { error(int16_t(e), true); return true; }
        if (*p && (*p++ != ',' || std::strspn(p, " 0123456789") != std::strlen(p) || !Header::number(p, start) || start < 0 || start > 4294967295.0 || start != uint32_t(start))) { error(-222, true); return true; }
        uint32_t count = 0, total = 0;
        const int e = backend_.fileList(folder, uint32_t(start), list, sizeof list, count, total);
        if (e) error(int16_t(e), true);
        else if (!count && start < total) error(-223, true); // One name longer than a reply: paging cannot pass it.
        else reply("%lu,%lu%s%s", (unsigned long)total, (unsigned long)count, *list ? "," : "", list);
    } else if (match("MMEMory:DATA")) { // "FOLDER/NAME": the file as a definite block.
        const char *p = c.argument;
        char file[200];
        uint32_t bytes = 0;
        if (!c.query) { error(-109); return true; }
        if (const int e = path(p, true, file, sizeof file)) { error(int16_t(e), true); return true; }
        if (*p) { error(-108, true); return true; }
        endFile();
        if (const int e = backend_.fileOpen(file, bytes)) { error(int16_t(e), true); return true; }
        char count[16]; std::snprintf(count, sizeof count, "%lu", (unsigned long)bytes);
        reply("#%u%s", unsigned(std::strlen(count)), count);
        fileBinary_ = true; fileAt_ = 0; fileBytes_ = bytes;
        if (!bytes) endFile();
    } else if (match("MMEMory:DELete")) {
        const char *p = c.argument;
        char file[200];
        if (c.query) { error(-108, true); return true; }
        if (const int e = path(p, true, file, sizeof file)) { error(int16_t(e)); return true; }
        if (*p) { error(-108); return true; }
        if (const int e = backend_.fileDelete(file)) error(int16_t(e));
    } else return false;
    return true;
}
} // namespace openosc::scpi
