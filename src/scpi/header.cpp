#include "openosc/scpi/header.hpp"
#include <cmath>
#include <cstring>

namespace openosc::scpi {
namespace {
char upper(char c) { return c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c; }
bool equal(const char *a, const char *b) {
    while (*a && *b && upper(*a) == upper(*b)) { ++a; ++b; }
    return !*a && !*b;
}
}
bool Header::matches(const char *actual, const char *pattern, unsigned *suffix) {
    if (!actual || !pattern) return false;
    while (*actual == ':') ++actual;
    while (*pattern == ':') ++pattern;
    while (*pattern) {
        const char *p = pattern, *a = actual;
        while (*p && *p != ':' && *p != '#') ++p;
        unsigned maximum = p - pattern, minimum = 0;
        for (unsigned i = 0; i < maximum; ++i) if (!(pattern[i] >= 'a' && pattern[i] <= 'z')) minimum = i + 1;
        while (*a && *a != ':') ++a;
        if (*p == '#') while (a > actual && a[-1] >= '0' && a[-1] <= '9') --a;
        const unsigned length = a - actual;
        if (length < minimum || length > maximum) return false;
        for (unsigned i = 0; i < length; ++i) if (upper(actual[i]) != upper(pattern[i])) return false;
        if (*p == '#') {
            if (*a < '0' || *a > '9') return false;
            unsigned number = 0;
            while (*a >= '0' && *a <= '9') {
                if (number > 1000) return false;
                number = number * 10 + *a++ - '0';
            }
            if (suffix) *suffix = number;
            ++p;
        }
        if (*a != *p) return false;
        if (!*p) return true;
        actual = a + 1; pattern = p + 1;
    }
    return !*actual;
}
bool Header::word(const char *actual, const char *pattern) {
    if (!actual || !pattern) return false;
    char copy[160]; const size_t size = std::strlen(actual);
    if (size >= sizeof copy) return false;
    if (size >= 2 && ((*actual == '"' && actual[size - 1] == '"') || (*actual == '\'' && actual[size - 1] == '\''))) {
        std::memcpy(copy, actual + 1, size - 2); copy[size - 2] = 0; actual = copy;
    }
    return matches(actual, pattern);
}
int Header::quoted(const char *&text, char *name, size_t size) {
    const char *p = text;
    if (*p != '"' && *p != '\'') return *p ? -104 : -109;
    const char quote = *p++;
    size_t length = 0;
    while (*p && *p != quote) {
        if (*p == '/' || *p == '\\' || length + 1 >= size) return -224;
        name[length++] = *p++;
    }
    if (*p != quote) return -104;
    if (!length) return -224;
    name[length] = 0; text = p + 1;
    return 0;
}
bool Header::boolean(const char *argument, bool &value) {
    if (!argument) return false;
    if (equal(argument, "1") || equal(argument, "ON") || equal(argument, "TRUE")) { value = true; return true; }
    if (equal(argument, "0") || equal(argument, "OFF") || equal(argument, "FALSE")) { value = false; return true; }
    return false;
}
bool Header::number(const char *argument, double &value, Unit unit) {
    if (!argument) return false;
    const char *end = argument;
    while (*end == ' ' || *end == '\t') ++end;
    const bool negative = *end == '-'; if (*end == '+' || *end == '-') ++end;
    double number = 0; unsigned digits = 0, decimals = 0; bool point = false;
    while ((*end >= '0' && *end <= '9') || (*end == '.' && !point)) {
        if (*end == '.') point = true;
        else { number = number * 10 + (*end - '0'); ++digits; if (point) ++decimals; }
        if (++end - argument > 159 || !std::isfinite(number)) return false;
    }
    if (!digits) return false;
    const bool nonzero = number != 0;
    int exponent = 0;
    if (*end == 'e' || *end == 'E') {
        ++end; const bool minus = *end == '-'; if (*end == '+' || *end == '-') ++end;
        if (*end < '0' || *end > '9') return false;
        while (*end >= '0' && *end <= '9') { exponent = exponent * 10 + *end++ - '0'; if (exponent > 400) return false; }
        if (minus) exponent = -exponent;
    }
    exponent -= int(decimals);
    while (exponent > 0) { number *= 10; --exponent; }
    while (exponent < 0) { number /= 10; ++exponent; }
    if (!std::isfinite(number) || (nonzero && number == 0)) return false;
    if (negative) number = -number;
    while (*end == ' ' || *end == '\t') ++end;
    double multiplier = 1;
    if (*end) {
        if (unit == Unit::Scalar) {
            if (equal(end, "K")) multiplier = 1e3;
            else if (equal(end, "M")) multiplier = 1e6;
            else if (equal(end, "G")) multiplier = 1e9;
            else return false;
        } else if (unit == Unit::Voltage) {
            if (equal(end, "MV")) multiplier = 1e-3;
            else if (equal(end, "UV")) multiplier = 1e-6;
            else if (!equal(end, "V")) return false;
        } else if (unit == Unit::Time) {
            if (equal(end, "MS")) multiplier = 1e-3;
            else if (equal(end, "US")) multiplier = 1e-6;
            else if (equal(end, "NS")) multiplier = 1e-9;
            else if (!equal(end, "S")) return false;
        } else if (unit == Unit::Frequency) {
            if (equal(end, "KHZ")) multiplier = 1e3;
            else if (equal(end, "MHZ")) multiplier = 1e6;
            else if (!equal(end, "HZ")) return false;
        } else if (unit == Unit::Percent && !equal(end, "%")) return false;
    }
    const double scaled = number * multiplier;
    if (!std::isfinite(scaled) || (number != 0 && scaled == 0)) return false;
    value = scaled; return true;
}
} // namespace openosc::scpi
