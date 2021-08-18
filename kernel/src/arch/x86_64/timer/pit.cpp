#include "pit.h"

#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/ports.h"

#include "lib/function.h"
#include "proc/tasking/process.h"

uint32_t tick = 0;
static uint64_t task = 0;
static uint8_t task_was_on = 0;

void StartTask(uint64_t index)
{
	if(!task_was_on) return;
	task = index;
}

void EnableTasking()
{
	task_was_on = 1;
	task = 1;
}

uint32_t frequency;
double timeSinceBoot = 0.0;

static void timer_callback(Registers* regs) 
{
    tick++;    
    timeSinceBoot += 1.0 / (double)frequency;
    
    if(task)
    {
        __outb(PIC1_COMMAND, PIC_EOI); //Send EOI because we aren't returning from here
        Schedule();
    }
    
    UNUSED(regs);
}

extern IDTR idtr;
#include "lib/stdio.h"

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