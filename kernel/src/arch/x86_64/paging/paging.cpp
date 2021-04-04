#include "paging.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

PageTableManager KernelDirectory(nullptr);

extern uint64_t kernelStart, kernelEnd;
extern uint8_t fromUEFI;

extern IDTR idtr;
void InitPaging(MemoryMap* memMap)
{
    PageFrameAllocator::SharedAllocator()->ReadMemoryMap(memMap);

    PageTable* kernelDirectory = (PageTable*)PageFrameAllocator::SharedAllocator()->RequestPage();
    memset(kernelDirectory, 0, 0x1000);

    KernelDirectory = PageTableManager(kernelDirectory);

    for(uint64_t i = 0; i < memMap->GetTotalSystemMemory(); i += 0x1000)
    {
        KernelDirectory.MapMemory(i, i);
    }

    idtr.SetGate(14, IDT_TA_InterruptGate, (uint64_t)PageFaultHandler);

    KernelDirectory.SetAsCurrent();
}

void PageFaultHandler(Registers* regs)
{
    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r" (cr2));

    uint32_t flags = regs->err_code;

    int present = !(flags & 0x1);
    int rw = flags & 0x2;
    int us = flags & 0x4;
    int reserved = flags & 0x8;
    int inst = flags & 0x10;

    kprintf("\n\n\n---------------------------------------------");

    kprintf("\n\n\nPAGE FAULT: at virtual address %x\n", cr2, flags);

    kprintf("Flags:\n");
    kprintf("Present: %d\n", present);
    kprintf("Write: %d\n", rw);
    kprintf("User access: %d\n", us);
    kprintf("Reserved: %d\n", reserved);
    kprintf("Instruction fetch: %d\n", inst);

    kprintf("\n\n\n---------------------------------------------\n\n\n");

    while(1) asm("hlt");
}