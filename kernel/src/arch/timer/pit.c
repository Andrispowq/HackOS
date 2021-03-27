#include "pit.h"

#include "arch/interrupts/idt.h"
#include "arch/interrupts/isr.h"
#include "arch/ports.h"

#include "lib/function.h"

uint32_t tick = 0;

uint32_t frequency;
double timeSinceBoot = 0.0;

static void timer_callback(registers_t* regs) 
{
    tick++;    
    timeSinceBoot += 1.0 / (double)frequency;
    
    UNUSED(regs);
}

void InitTimer(uint32_t freq) 
{
    frequency = freq;

    RegisterInterruptHandler(IRQ0, timer_callback);

    uint32_t divisor = 1193180 / freq;
    uint8_t low  = (uint8_t)(divisor & 0xFF);
    uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);

    __outb(0x43, 0x36);
    __outb(0x40, low);
    __outb(0x40, high);
}

void SleepFor(uint32_t milliseconds)
{
    SleepForSeconds((double)milliseconds / 1000.0);
}

void SleepForSeconds(double seconds)
{
    double end = timeSinceBoot + seconds;
    while(timeSinceBoot < end)
    {
        asm("hlt");
    }
}