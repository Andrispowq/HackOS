#include "paging.h"

#include "lib/memory.h"
#include "lib/string.h"
#include "lib/stdio.h"

PageTableManager KernelDirectory(nullptr);
PageTableManager* CurrentDirectory = nullptr;

extern uint64_t kernelStart, kernelEnd;
extern bool fromUEFI;

extern IDTR idtr;
bool user_nx_enabled;

static void ConfigurePageProtection()
{
    uint32_t a, b, c, d;
    asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000000U), "c"(0));
    if (a >= 0x80000001U)
    {
        asm volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0x80000001U), "c"(0));
        user_nx_enabled = d & (1U << 20);
    }

    asm volatile("rdmsr" : "=a"(a), "=d"(d) : "c"(0xc0000080U));
    a &= ~1U; // EFER.SCE=0: user SYSCALL must #UD, never jump to an unset LSTAR.
    if (user_nx_enabled)
    {
        a |= 1U << 11;
    }

    asm volatile("wrmsr" : : "a"(a), "d"(d), "c"(0xc0000080U) : "memory");
    uint64_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1ULL << 16; // Supervisor writes also respect read-only pages.
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

void InitPaging(MemoryMap* memMap)
{
    ConfigurePageProtection();
    PageFrameAllocator::SharedAllocator()->ReadMemoryMap(memMap);

    PageTable* kernelDirectory = (PageTable*)PageFrameAllocator::SharedAllocator()->RequestPage();
    memset(kernelDirectory, 0, 0x1000);

    KernelDirectory = PageTableManager(kernelDirectory);

    // Keep the low boot handoff/BIOS area, then map actual regions instead of a sum of lengths.
    for (uint64_t address = 0; address < 0x100000; address += 4096)
    {
        KernelDirectory.MapMemory(address, address);
    }

    for (uint64_t i = 0; i < memMap->GetEntryCount(); ++i)
    {
        auto entry = memMap->GetEntry(i);
        if (!entry->size || entry->size > ~0ULL - entry->address)
        {
            continue;
        }

        if (fromUEFI ? (entry->type == 0 || entry->type == 8 || entry->type >= 11)
                     : (entry->type == 2 && entry->address >= 0x100000000ULL))
        {
            continue;
        }

        uint64_t first = entry->address & ~4095ULL;
        uint64_t last = (entry->address + entry->size - 1) & ~4095ULL;
        for (uint64_t address = first;; address += 4096)
        {
            KernelDirectory.MapMemory(address, address);
            if (address == last)
            {
                break;
            }
        }
    }

    RegisterInterruptHandler(14, PageFaultHandler);

    KernelDirectory.SetAsCurrent();
}

void PageFaultHandler(Registers* regs)
{
    uint64_t cr2;
    asm volatile("mov %%cr2, %0" : "=r"(cr2));

    uint32_t flags = regs->err_code;

    int present = !(flags & 0x1);
    int rw = flags & 0x2;
    int us = flags & 0x4;
    int reserved = flags & 0x8;
    int inst = flags & 0x10;

    kprintf("\n\n\n---------------------------------------------");

    kprintf("\n\n\nPAGE FAULT: at virtual address %x, physical address %x, RIP: %x\n", cr2,
            CurrentDirectory->PhysicalAddress(cr2) + cr2 & 0xFFF, flags, regs->rip);

    kprintf("Flags:\n");
    kprintf("Present: %s\n", (present > 0) ? "true" : "false");
    kprintf("Write: %s\n", (rw > 0) ? "true" : "false");
    kprintf("User access: %s\n", (us > 0) ? "true" : "false");
    kprintf("Reserved: %s\n", (reserved > 0) ? "true" : "false");
    kprintf("Instruction fetch: %s\n", (reserved > 0) ? "true" : "false");

    kprintf("\n\n\n---------------------------------------------\n\n\n");

    while (1)
    {
        asm("hlt");
    }
}
