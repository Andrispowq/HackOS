#include "rtc.h"
#include "cpu/ports.h"
#include "screen.h"

void rtc_init(struct tm* kernel_time)
{
	rtc_get_time(kernel_time);
}

uint8_t rtc_get_year()
{
	__outb(0x70, 0x09);
	return __inb(0x71);
}

uint8_t rtc_get_month()
{
	__outb(0x70, 0x08);
	return __inb(0x71);
}

uint8_t rtc_get_day()
{
	__outb(0x70, 0x07);
	return __inb(0x71);
}

uint8_t rtc_get_weekday()
{
	__outb(0x70, 0x06);
	return __inb(0x71);
}

uint8_t rtc_get_hour()
{
	__outb(0x70, 0x04);
	return __inb(0x71);
}

uint8_t rtc_get_minute()
{
	__outb(0x70, 0x02);
	return __inb(0x71);
}

uint8_t rtc_get_second()
{
	__outb(0x70, 0x00);
	return __inb(0x71);
}

void rtc_get_time(struct tm* tm)
{
	tm->year = rtc_get_year();
	tm->month = rtc_get_month();
	tm->day = rtc_get_day();
	tm->hour = rtc_get_hour();
	tm->minute = rtc_get_minute();
	tm->second = rtc_get_second();
}