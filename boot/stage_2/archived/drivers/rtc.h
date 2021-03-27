#ifndef __RTC_H_
#define __RTC_H_

#include "../libc/stdint.h"

struct tm
{
    uint32_t year;
    uint32_t month;
    uint32_t day;
    uint32_t hour;
    uint32_t minute;
    uint32_t second;
};

void rtc_init(struct tm* kernel_time);
void rtc_get_time(struct tm* tm);

#endif