#include "rtc.h"
#include "../cpu/ports.h"
#include "screen.h"

void rtc_init(struct tm* kernel_time)
{
	rtc_get_time(kernel_time);
}

uint8_t rtc_get_year()
{
	outb(0x70, 0x09);
	return inb(0x71);
}

uint8_t rtc_get_month()
{
	outb(0x70, 0x08);
	return inb(0x71);
}

uint8_t rtc_get_day()
{
	outb(0x70, 0x07);
	return inb(0x71);
}

uint8_t rtc_get_weekday()
{
	outb(0x70, 0x06);
	return inb(0x71);
}

uint8_t rtc_get_hour()
{
	outb(0x70, 0x04);
	return inb(0x71);
}

uint8_t rtc_get_minute()
{
	outb(0x70, 0x02);
	return inb(0x71);
}

uint8_t rtc_get_second()
{
	outb(0x70, 0x00);
	return inb(0x71);
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