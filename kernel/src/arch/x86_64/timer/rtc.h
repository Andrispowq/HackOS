#ifndef __RTC_H_
#define __RTC_H_

#include "lib/stdint.h"

struct tm
{
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
};

void InitRTC(struct tm* kernel_time);
void GetTimeRTC(struct tm* tm);

#endif