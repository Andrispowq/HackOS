#include "kernel.h"

#include "drivers/screen/screen.h"

#include "arch/interrupts/idt.h"
#include "arch/interrupts/isr.h"
#include "arch/gdt/gdt.h"
#include "arch/timer/pit.h"
#include "arch/paging/paging.h"
#include "arch/ports.h"

#include "memory/memory_map.h"
#include "memory/heap.h"

#include "acpi/rsdp.h"
#include "acpi/system_table.h"

#include "lib/stdio.h"
#include "lib/font/font.h"

#include "shell/shell.h"

struct KernelInfo
{
    FRAMEBUFFER framebuffer;
    PSF1_FONT* font;
    MemoryMapEntry* memMap;
    uint64_t memMapEntrySize;
    void* rsdp;
    uint8_t memMapEntries;
    uint8_t rsdp_version;
    uint8_t booted_from_BIOS;
};

uint8_t fromUEFI = 0;

extern uint32_t tick;
extern double timeSinceBoot;

extern uint32_t kernel_start, end;

uint64_t kernelStart = (uint64_t)&kernel_start;
uint64_t kernelEnd = (uint64_t)&end;

int kernel_main(struct KernelInfo* info)
{
    fromUEFI = 1 - info->booted_from_BIOS;

    if(fromUEFI)
    {
        asm("cli");
        InstallGDT();
    }

    InstallISR();
    InitPaging(info->memMap, info->memMapEntries);
    
    InitDisplay(info->framebuffer, info->font);
    clrscr();

    kprintf("Welcome to the HackOS kernel!\n\n");

    RSDP2* rsdp = info->rsdp;
    kprintf("RSDP information: \n");
    kprintf("\tLocation: 0x%x\n", rsdp);
    kprintf("\tSigniture: ");
    for(uint64_t i = 0; i < 8; i++)
        kprintf("%c", rsdp->rsdp1.Signature[i]);
    kprintf("\n");
    kprintf("\tChecksum: %d\n", rsdp->rsdp1.Checksum);
    kprintf("\tOEMID: ");
    for(uint64_t i = 0; i < 6; i++)
        kprintf("%c", rsdp->rsdp1.OEMID[i]);
    kprintf("\n");
    kprintf("\tRevision: %d\n", rsdp->rsdp1.Revision);
    kprintf("\tRSDT Address: 0x%x\n", rsdp->rsdp1.RSDTAddress);
    kprintf("\tLength: %d\n", rsdp->Length);
    kprintf("\tXSDT Address: 0x%x\n", rsdp->XSDTAddress);
    kprintf("\tExtended checksum: %d\n\n", rsdp->ExtendedChecksum);

    SDTHeader* xsdt = (SDTHeader*)rsdp->XSDTAddress;
    uint64_t entries = (xsdt->Length - sizeof(SDTHeader)) / 8;

    kprintf("System tables: ");
    for (uint64_t t = 0; t < entries; t++)
    {
        SDTHeader* newSDTHeader = (SDTHeader*)*(uint64_t*)((uint64_t)xsdt + sizeof(SDTHeader) + (t * 8));
        for (int i = 0; i < 4; i++)
        {
            kprintf("%c", newSDTHeader->Signature[i]);
        }
        kprintf(" ");
    }

    kprintf("\n\n");

    SDTHeader* mcfg = FindTable(xsdt, "MCFG");
    kprintf("\tMCFG Table: 0x%x\n", mcfg);
    SDTHeader* apic = FindTable(xsdt, "APIC");
    kprintf("\tAPIC Table: 0x%x\n", apic);
    SDTHeader* hpet = FindTable(xsdt, "HPET");
    kprintf("\tHPET Table: 0x%x\n", hpet);
    SDTHeader* fadt = FindTable(xsdt, "FADT");
    kprintf("\tFADT Table: 0x%x\n\n", fadt);

    uint64_t usedMemory = GetUsedMemory();
    uint64_t freeMemory = GetFreeMemory();
    uint64_t reservedMemory = GetReservedMemory();

    double usedMemoryMegabytes = usedMemory / (1024.0 * 1024.0);
    double usedMemoryGigabytes = usedMemory / (1024.0 * 1024.0 * 1024.0);
    double freeMemoryMegabytes = freeMemory / (1024.0 * 1024.0);
    double freeMemoryGigabytes = freeMemory / (1024.0 * 1024.0 * 1024.0);
    double reservedMemoryMegabytes = reservedMemory / (1024.0 * 1024.0);
    double reservedMemoryGigabytes = reservedMemory / (1024.0 * 1024.0 * 1024.0);

    double totalSystemMemory = usedMemoryMegabytes + freeMemoryMegabytes + reservedMemoryMegabytes;

    kprintf("Memory info: \n");
    kprintf("\tUsed memory: 0x%x bytes, 0x%x kilobytes, %f megabytes, %f gigabytes.\n", usedMemory,
        usedMemory / 1024, usedMemoryMegabytes, usedMemoryGigabytes);
    kprintf("\tFree memory: 0x%x bytes, 0x%x kilobytes, %f megabytes, %f gigabytes.\n", freeMemory,
        freeMemory / 1024, freeMemoryMegabytes, freeMemoryGigabytes);
    kprintf("\tReserved memory: 0x%x bytes, 0x%x kilobytes, %f megabytes, %f gigabytes.\n", reservedMemory,
        reservedMemory / 1024, reservedMemoryMegabytes, reservedMemoryGigabytes);

    kprintf("\tTotal system memory: %f megabytes.\n\n", totalSystemMemory);

    kprintf("Initialising the kernel heap!\n");
    InitialiseHeap((void*)0x0000100000000000, 0x10);

    uint32_t* mem = (uint32_t*)kmalloc(8);
    *mem = 10;
    kprintf("\tMemory address: 0x%x\n", mem);
    uint32_t* mem2 = (uint32_t*)kmalloc(8);
    *mem2 = 10;
    kprintf("\tMemory address: 0x%x\n", mem2);
    kfree(mem);
    kfree(mem2);
    uint32_t* mem3 = (uint32_t*)kmalloc(16);
    *mem3 = 10;
    kprintf("\tMemory address: 0x%x\n", mem3);
    kfree(mem3);

    uint64_t phys;
    uint64_t memory = (uint64_t)kmalloc_p(0x1000, &phys);
    kprintf("\tMem: 0x%x, 0x%x\n", memory, phys);
    uint64_t phys2;
    uint64_t memory2 = (uint64_t)kmalloc_p(0x1000, &phys2);
    kprintf("\tMem: 0x%x, 0x%x\n", memory2, phys2);
    uint64_t phys3;
    uint64_t memory3 = (uint64_t)kmalloc_p(0x1000, &phys3);
    kprintf("\tMem: 0x%x, 0x%x\n\n", memory3, phys3);

    InstallIRQ();

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