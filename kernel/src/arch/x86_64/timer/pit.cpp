#include "pit.h"
#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/ports.h"

volatile uint64_t tick;
static uint32_t frequency;
static void timer_callback(Registers*) { ++tick; }

void InitTimer(uint32_t freq)
{
    frequency = freq;
    RegisterInterruptHandler(IRQ0, timer_callback);
    uint32_t divisor = 1193180 / freq;
    __outb(0x43, 0x36);
    __outb(0x40, (uint8_t)divisor);
    __outb(0x40, (uint8_t)(divisor >> 8));
}

void SleepFor(uint32_t milliseconds)
{
    uint64_t start = tick;
    uint64_t duration = ((uint64_t)milliseconds * frequency + 999) / 1000;
    while (tick - start < duration) asm volatile("hlt" ::: "memory");
}
void SleepForSeconds(double seconds)
{
    if (seconds > 0) SleepFor((uint32_t)(seconds * 1000));
}
