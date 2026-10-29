#pragma once
#include "hackos/abi.h"

// FAT timestamps have no timezone and two-second precision. Do not invent dates
// for root or malformed/absent directory metadata.
inline void FormatFileTime(uint32_t value, char (&text)[20])
{
    unsigned date = value >> 16, year = 1980 + (date >> 9), month = (date >> 5) & 15, day = date & 31;
    unsigned hour = (value >> 11) & 31, minute = (value >> 5) & 63, second = (value & 31) * 2;
    const unsigned days[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
    if (!month || month > 12 || !day || day > days[month] + (month == 2 && leap) || hour > 23 || minute > 59 ||
        second > 59)
    {
        const char* unknown = "Unknown";
        unsigned i = 0;
        do
        {
            text[i] = unknown[i];
        } while (unknown[i++]);
        return;
    }
    const char* pattern = "0000-00-00 00:00:00";
    for (unsigned i = 0; i < 20; ++i)
    {
        text[i] = pattern[i];
    }
    text[0] += year / 1000;
    text[1] += year / 100 % 10;
    text[2] += year / 10 % 10;
    text[3] += year % 10;
    const unsigned values[] = {month, day, hour, minute, second}, positions[] = {5, 8, 11, 14, 17};
    for (unsigned i = 0; i < 5; ++i)
    {
        text[positions[i]] += values[i] / 10;
        text[positions[i] + 1] += values[i] % 10;
    }
}
