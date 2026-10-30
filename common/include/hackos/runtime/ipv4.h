#pragma once
#include "hackos/types.h"

namespace Hackos
{
// Canonical decimal dotted quad: no whitespace, signs, octal or shorthand.
inline bool ParseIPv4(const char* text, uint8_t (&address)[4])
{
    if (!text) return false;
    for (unsigned part = 0; part < 4; ++part)
    {
        unsigned value = 0, digits = 0;
        bool zero = *text == '0';
        while (*text >= '0' && *text <= '9')
        {
            if (++digits > 3 || (zero && digits > 1)) return false;
            value = value * 10 + (*text++ - '0');
        }
        if (!digits || value > 255) return false;
        address[part] = (uint8_t)value;
        if (part == 3) return *text == 0;
        if (*text++ != '.') return false;
    }
    return false;
}
}
