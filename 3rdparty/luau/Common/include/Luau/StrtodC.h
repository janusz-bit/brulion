// Brulion: added to the vendored Luau (see ../../../CMakeLists.txt). MIT, like Luau.
#pragma once

#include <clocale>
#include <cstddef>
#include <cstdlib>
#include <cstring>

// strtod that always reads '.' as the decimal point, whatever the C locale. A Qt application sets
// LC_ALL from the environment, and with a Polish locale (decimal comma) strtod stops at '.': Luau's
// parser then reports "Malformed number" for 82.5 and tonumber("82.5") is nil. The number's text is
// copied with '.' replaced by the locale's decimal point, and the end pointer mapped back.
inline double luau_strtod_c(const char* s, char** end)
{
    const char* point = localeconv()->decimal_point;
    if (!point || !point[0] || (point[0] == '.' && point[1] == 0))
        return strtod(s, end);
    const size_t pointLength = strlen(point);
    const char* p = s;
    while (*p == ' ' || (*p >= '\t' && *p <= '\r'))
        p++;
    const char* start = p;
    char buffer[512];
    size_t n = 0;
    for (; *p; p++)
    {
        const char c = *p;
        const bool part = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '+' || c == '-' || c == '.';
        if (!part || n + pointLength + 1 >= sizeof(buffer))
            break;
        if (c == '.')
        {
            memcpy(buffer + n, point, pointLength);
            n += pointLength;
        }
        else
        {
            buffer[n++] = c;
        }
    }
    buffer[n] = 0;
    char* bufferEnd = nullptr;
    const double value = strtod(buffer, &bufferEnd);
    const size_t consumed = size_t(bufferEnd - buffer);
    size_t inBuffer = 0;
    const char* source = start;
    while (inBuffer < consumed)
    {
        inBuffer += *source == '.' ? pointLength : 1;
        source++;
    }
    if (end)
        *end = const_cast<char*>(consumed ? source : s);
    return value;
}
