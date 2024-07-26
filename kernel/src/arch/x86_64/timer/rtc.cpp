#include "rtc.h"

#include "arch/x86_64/ports.h"

uint8_t GetYearRTC()
{
	__outb(0x70, 0x09);
	return __inb(0x71);
}

uint8_t GetMonthRTC()
{
	__outb(0x70, 0x08);
	return __inb(0x71);
}

uint8_t GetDayRTC()
{
	__outb(0x70, 0x07);
	return __inb(0x71);
}

uint8_t GetWeekdayRTC()
{
	__outb(0x70, 0x06);
	return __inb(0x71);
}

uint8_t GetHourRTC()
{
	__outb(0x70, 0x04);
	return __inb(0x71);
}

uint8_t GetMinuteRTC()
{
	__outb(0x70, 0x02);
	return __inb(0x71);
}

uint8_t GetSecondRTC()
{
	__outb(0x70, 0x00);
	return __inb(0x71);
}

void InitRTC(struct tm* kernel_time)
{
	GetTimeRTC(kernel_time);
}

void GetTimeRTC(struct tm* tm)
{
	tm->year = GetYearRTC();
	tm->month = GetMonthRTC();
	tm->day = GetDayRTC();
	tm->hour = GetHourRTC();
	tm->minute = GetMinuteRTC();
	tm->second = GetSecondRTC();
}