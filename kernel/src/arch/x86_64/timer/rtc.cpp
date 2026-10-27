#include "rtc.h"
#include "rtc_sample.h"
#include "arch/x86_64/ports.h"
#include "arch/x86_64/interrupts/guard.h"

static uint8_t centuryRegister;

void SetRTCCenturyRegister(uint8_t index)
{
    centuryRegister = index >= 0x0e && index < 0x80 ? index : 0;
}

bool TryGetTimeRTC(tm* value)
{
    return SampleRTC(
        [](uint8_t index)
        {
            InterruptGuard guard;
            __outb(0x70, index);
            return __inb(0x71);
        },
        centuryRegister, *value);
}

void GetTimeRTC(tm* value)
{
    // FAT has no error channel for timestamps. Use its epoch if the RTC fails.
    *value = {80, 0, 1, 0, 0, 0};
    TryGetTimeRTC(value);
}

void InitRTC(tm* value)
{
    GetTimeRTC(value);
}
