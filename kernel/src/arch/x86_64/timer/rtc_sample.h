#pragma once
#include "rtc.h"

struct RTCSample
{
    uint8_t second, minute, hour, day, month, year, century, mode;
};

inline bool DecodeRTC(const RTCSample& raw, tm& value)
{
    bool binary = raw.mode & 4;
    auto number = [binary](uint8_t n) -> unsigned
    {
        if (binary)
        {
            return n;
        }
        if ((n & 15) > 9 || (n >> 4) > 9)
        {
            return 1000;
        }
        return (n >> 4) * 10 + (n & 15);
    };
    unsigned second = number(raw.second), minute = number(raw.minute);
    unsigned hour = number(raw.hour & 0x7f), day = number(raw.day), month = number(raw.month);
    unsigned year = number(raw.year), century = raw.century ? number(raw.century) : 20;
    if (!(raw.mode & 2))
    {
        if (hour < 1 || hour > 12)
        {
            return false;
        }
        hour = hour % 12 + ((raw.hour & 0x80) ? 12 : 0);
    }
    else if (raw.hour & 0x80)
    {
        return false;
    }

    if (year > 99 || century < 19 || century > 99 || month < 1 || month > 12 || second > 59 || minute > 59 || hour > 23)
    {
        return false;
    }
    year += century * 100;
    const unsigned days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    unsigned maximum = days[month - 1];
    if (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))
    {
        ++maximum;
    }
    if (day < 1 || day > maximum)
    {
        return false;
    }

    // Internal tm follows the FAT timestamp consumer: years since 1900,
    // zero-based month. The public clock syscall converts to calendar fields.
    value = {year - 1900, month - 1, day, hour, minute, second};
    return true;
}

template <typename Reader> bool SampleRTC(Reader read, uint8_t centuryRegister, tm& value)
{
    RTCSample previous = {};
    bool havePrevious = false;
    for (unsigned attempt = 0; attempt < 1024; ++attempt)
    {
        if (read(0x0a) & 0x80)
        {
            havePrevious = false;
            continue;
        }

        RTCSample current = {read(0),
                             read(2),
                             read(4),
                             read(7),
                             read(8),
                             read(9),
                             centuryRegister ? read(centuryRegister) : (uint8_t)0,
                             read(0x0b)};
        if (read(0x0a) & 0x80)
        {
            havePrevious = false;
            continue;
        }

        if (havePrevious && current.second == previous.second && current.minute == previous.minute &&
            current.hour == previous.hour && current.day == previous.day && current.month == previous.month &&
            current.year == previous.year && current.century == previous.century && current.mode == previous.mode)
        {
            return DecodeRTC(current, value);
        }

        previous = current;
        havePrevious = true;
    }

    return false;
}
