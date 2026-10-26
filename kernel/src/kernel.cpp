#include "kernel.h"

#include "shell/shell.h"
#include "init/kernelInit.h"

#include "acpi/fadt.h"
#include "proc/tasking/process.h"

#include "drivers/keyboard/keyboard.h"
#include "shell/line_buffer.h"
#include "arch/x86_64/interrupts/guard.h"

extern uint32_t kernel_start, end;

uint64_t kernelStart = (uint64_t)&kernel_start;
uint64_t kernelEnd = (uint64_t)&end;

KernelInfo* kInfo;

char system_input[256];

extern "C" int kernel_main(KernelInfo* info)
{
    kInfo = info;

    InitialiseDisplay(info);
    InitialiseKernel(info);

    Display::SharedDisplay()->DrawBackbuffer();

    while (true) { ReapExitedProcesses(); asm volatile("hlt" ::: "memory"); }
}

#include "fs/vfs.h"
#include "drivers/screen/window_manager.h"

extern WindowManager* KernelWindowManager;

void kernel_task(void*)
{
    kprintf("\nHackOS kernel shell. Type 'help' for commands.\n");
    shell_command((char*)"");
    LineBuffer line;
    bool discardLine = false;
    for (;;)
    {
        char ch;
        for (;;)
        {
            KeyboardRead result = ReadKeyboard(ch);
            if (result == KeyboardRead::Empty) break;
            if (result == KeyboardRead::Overflow)
            {
                line.Reset();
                discardLine = true;
                kprintf("\nKeyboard queue overflow; press Enter to start a new command.\n");
                continue;
            }
            if (discardLine)
            {
                if (ch == '\n') { discardLine = false; shell_command((char*)""); }
                continue;
            }
            switch (line.Push(ch))
            {
            case LineBuffer::Echo: kprintf("%c", ch); break;
            case LineBuffer::Erase: kprintf_backspace(); break;
            case LineBuffer::Submit:
                kprintf("\n");
                user_input(line.Text());
                line.Reset();
                break;
            case LineBuffer::Overflow:
                kprintf("\nCommand too long (maximum 255 characters).\n");
                line.Reset();
                shell_command((char*)"");
                break;
            default: break;
            }
        }
        {
            PreemptionGuard guard;
            auto display = Display::SharedDisplay();
            if (display->dirty) display->DrawBackbuffer();
        }
        SleepFor(10);
    }
}

void user_input(char* input)
{
    PreemptionGuard guard;
    strcpy(system_input, input);

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