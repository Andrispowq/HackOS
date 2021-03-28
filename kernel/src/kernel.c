#include "kernel.h"

#include "shell/shell.h"
#include "init/kernelInit.h"

extern uint32_t tick;
extern double timeSinceBoot;

extern uint32_t kernel_start, end;

uint64_t kernelStart = (uint64_t)&kernel_start;
uint64_t kernelEnd = (uint64_t)&end;

int kernel_main(struct KernelInfo* info)
{
    InitialiseDisplay(info);

    kprintf("Welcome to the HackOS kernel!\n\n");

    InitialiseKernel(info);

    kprintf("Finished the initialisation!\n");
    kprintf("Type 'help' for help!\n> ");

    while(1)
    {
        asm("hlt");
    }
}

void user_input(char* input)
{
    if (strcmp(input, "shutdown") == 0) 
    {
        kprintf("Stopping the CPU. Bye!\n");

        //Bochs/older QEMU versions
        __outw(0xB004, 0x2000);
        //Newer QEMU versions
        __outw(0x0604, 0x2000);
        //VirtualBox
        __outw(0x4004, 0x3400);
    }
    else if (strcmp(input, "restart") == 0) 
    {
        kprintf("Restarting the CPU. Bye!\n");

        memset(input, 0, 255);

        //Flush the IRQ1 buffer
        unsigned temp;
        do
        {
           temp = (unsigned)__inb(0x64);
           if((temp & 0x01) != 0)
           {
              (void)__inb(0x60);
              continue;
           }
        } 
        while((temp & 0x02) != 0);
        
        //Restart
        __outb(0x70, 0x8F);
        __outb(0x71, 0x00);
        __outb(0x70, 0x00);
        __outb(0x92, __inb(0x92) | 0x1);
    }
    else
    {
        shell_command(input);   
    }
}