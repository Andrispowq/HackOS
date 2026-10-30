#include "kernel.h"
#include "drivers/serial/serial.h"
#include "drivers/net/e1000.h"

#include "init/kernelInit.h"

#include "acpi/fadt.h"
#include "proc/tasking/process.h"

#include "drivers/keyboard/keyboard.h"
#include "drivers/screen/window_manager.h"

#include "arch/x86_64/interrupts/guard.h"

extern uint32_t kernel_start, end;

uint64_t kernelStart = (uint64_t)&kernel_start;
uint64_t kernelEnd = (uint64_t)&end;

KernelInfo* kInfo;

extern "C" int kernel_main(KernelInfo* info)
{
    InitialiseSerial();
    SerialText(info->booted_from_BIOS ? "HackOS kernel entry: BIOS\n" : "HackOS kernel entry: UEFI\n");
    kInfo = info;

    InitialiseDisplay(info);
    InitialiseKernel(info);

    Display::SharedDisplay()->DrawBackbuffer();

    while (true)
    {
        ReapExitedProcesses();
        Net::Poll();
        {
            PreemptionGuard guard;
            KernelWindowManager->Update();

            auto display = Display::SharedDisplay();
            display->Refresh();
        }

        asm volatile("hlt" ::: "memory");
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
