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

extern "C" 
{
static void timer_callback(/*Registers* regs*/) 
{
    if(!task)
    {
	    asm volatile("push %r15");
	    asm volatile("push %r14");
	    asm volatile("push %r13");
	    asm volatile("push %r12");
	    asm volatile("push %r11");
	    asm volatile("push %r10");
	    asm volatile("push %r9");
	    asm volatile("push %r8");

	    asm volatile("push %rax");
	    asm volatile("push %rcx");
	    asm volatile("push %rdx");
	    asm volatile("push %rbx");
	    asm volatile("push %rbp");
	    asm volatile("push %rsp");
	    asm volatile("push %rsi");
	    asm volatile("push %rdi");

	    asm volatile("sub $256, %rsp");

        asm volatile("movdqa %xmm0, 240(%rsp)");
	    asm volatile("movdqa %xmm1, 224(%rsp)");
	    asm volatile("movdqa %xmm2, 208(%rsp)");
	    asm volatile("movdqa %xmm3, 192(%rsp)");
	    asm volatile("movdqa %xmm4, 176(%rsp)");
	    asm volatile("movdqa %xmm5, 160(%rsp)");
	    asm volatile("movdqa %xmm6, 144(%rsp)");
	    asm volatile("movdqa %xmm7, 128(%rsp)");

	    asm volatile("movdqa %xmm8, 112(%rsp)");
	    asm volatile("movdqa %xmm9, 96(%rsp)");
	    asm volatile("movdqa %xmm10, 80(%rsp)");
	    asm volatile("movdqa %xmm11, 64(%rsp)");
	    asm volatile("movdqa %xmm12, 48(%rsp)");
	    asm volatile("movdqa %xmm13, 32(%rsp)");
	    asm volatile("movdqa %xmm14, 16(%rsp)");
	    asm volatile("movdqa %xmm15, 0(%rsp)");

        tick++;    
        timeSinceBoot += 1.0 / (double)frequency;

	    asm volatile("movdqa 240(%rsp), %xmm0");
	    asm volatile("movdqa 224(%rsp), %xmm1");
	    asm volatile("movdqa 208(%rsp), %xmm2");
	    asm volatile("movdqa 192(%rsp), %xmm3");
	    asm volatile("movdqa 176(%rsp), %xmm4");
	    asm volatile("movdqa 160(%rsp), %xmm5");
	    asm volatile("movdqa 144(%rsp), %xmm6");
	    asm volatile("movdqa 128(%rsp), %xmm7");

	    asm volatile("movdqa 112(%rsp), %xmm8");
	    asm volatile("movdqa 96(%rsp), %xmm9");
	    asm volatile("movdqa 80(%rsp), %xmm10");
	    asm volatile("movdqa 64(%rsp), %xmm11");
	    asm volatile("movdqa 48(%rsp), %xmm12");
	    asm volatile("movdqa 32(%rsp), %xmm13");
	    asm volatile("movdqa 16(%rsp), %xmm14");
	    asm volatile("movdqa 0(%rsp), %xmm15");

	    asm volatile("add $256, %rsp");

	    asm volatile("out %%al, %%dx": :"d"(0x20), "a"(0x20)); // send EoI to master PIC

	    asm volatile("pop %rdi");
	    asm volatile("pop %rsi");
	    asm volatile("add $0x8, %rsp");
	    asm volatile("pop %rbp");
	    asm volatile("pop %rbx");
	    asm volatile("pop %rdx");
	    asm volatile("pop %rcx");
	    asm volatile("pop %rax");

	    asm volatile("pop %r8");
	    asm volatile("pop %r9");
	    asm volatile("pop %r10");
	    asm volatile("pop %r11");
	    asm volatile("pop %r12");
	    asm volatile("pop %r13");
	    asm volatile("pop %r14");
	    asm volatile("pop %r15");

        asm volatile("pop %rbp");
        asm volatile("sti");
        asm volatile("iretq");
    }
    else
    {
        //__outb(PIC1_COMMAND, PIC_EOI); //Send EOI because we aren't returning from here
        Schedule();
    }
    
    //UNUSED(regs);
}
}

extern IDTR idtr;
#include "lib/stdio.h"

void InitTimer(uint32_t freq) 
{
    frequency = freq;

    /*RegisterInterruptHandler*/idtr.SetGate(IRQ0, IDT_TA_InterruptGate, (uint64_t)timer_callback);

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