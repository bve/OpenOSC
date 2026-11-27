#pragma once
#include <cstdint>
#include <cstring>

namespace openosc::companion::web {
inline int hex(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return 10 + ch - 'a';
    return ch >= 'A' && ch <= 'F' ? 10 + ch - 'A' : -1;
}
inline bool field(const char *form, const char *name, char *out, size_t cap) {
    const size_t nameSize = std::strlen(name);
    while (*form) {
        const auto *end = std::strchr(form, '&');
        if (!end) end = form + std::strlen(form);
        if (size_t(end - form) > nameSize && form[nameSize] == '=' && !std::strncmp(form, name, nameSize)) {
            size_t n = 0;
            for (auto *p = form + nameSize + 1; p < end; ++p) {
                int value = uint8_t(*p);
                if (*p == '+') value = ' ';
                if (*p == '%') {
                    if (end - p < 3) return false;
                    const int high = hex(p[1]), low = hex(p[2]);
                    if (high < 0 || low < 0) return false;
                    value = high * 16 + low;
                    p += 2;
                }
                if (!value || n + 1 >= cap) return false;
                out[n++] = char(value);
            }
            out[n] = 0;
            return true;
        }
        form = *end ? end + 1 : end;
    }
    return false;
}
inline bool number(const char *form, const char *name, uint32_t &value, uint32_t max) {
    char text[11];
    if (!field(form, name, text, sizeof text) || !*text) return false;
    value = 0;
    for (const auto *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return false;
        const unsigned digit = *p - '0';
        if (digit > max || value > (max - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}
}
