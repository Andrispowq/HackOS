#include "kernel.h"

#include "shell/shell.h"
#include "init/kernelInit.h"

#include "acpi/fadt.h"
#include "proc/tasking/process.h"

extern uint32_t tick;
extern double timeSinceBoot;

extern uint32_t kernel_start, end;

uint64_t initial_rsp;
uint64_t kernelStart = (uint64_t)&kernel_start;
uint64_t kernelEnd = (uint64_t)&end;

KernelInfo* kInfo;

extern "C" int kernel_main(KernelInfo* info)
{
    kInfo = info;
   	asm volatile("mov %%rsp, %0" : "=r" (initial_rsp));	

    InitialiseDisplay(info);
    InitialiseKernel(info);

    while(true) asm("hlt");
}

void kernel_task()
{
    PrintAll();

    kprintf("Finished the initialisation!\n");
    kprintf("Type 'help' for help!\n");
    kprintf("root@root:~/$ ");

    _Kill();
    while(true) asm("hlt");
}

void user_input(char* input)
{
    if (strcmp(input, "shutdown") == 0) 
    {
        kprintf("Stopping the CPU. Bye!\n");

        ACPI::RSDP* rsdp = kInfo->rsdp;
        ACPI::FADT* fadt = (ACPI::FADT*)rsdp->GetSystemTable("FACP");
        fadt->Shutdown();
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

extern "C" void __cxa_pure_virtual()
{
    kprintf("Virtual function definition not found!\n");
}

extern "C" void __stack_chk_fail()
{
    kprintf("Stack check failed!\n");
}