#include "kernelInit.h"

bool fromUEFI = 0;

void PrintRSDPAndMemoryInfo(struct KernelInfo* info)
{
    RSDP* rsdp = (RSDP*)info->rsdp;
    kprintf("RSDP information: \n");
    kprintf("\tLocation: 0x%x\n", rsdp);
    kprintf("\tSigniture: ");
    for(uint64_t i = 0; i < 8; i++)
        kprintf("%c", rsdp->Signature[i]);
    kprintf("\n");
    kprintf("\tChecksum: %d\n", rsdp->Checksum);
    kprintf("\tOEMID: ");
    for(uint64_t i = 0; i < 6; i++)
        kprintf("%c", rsdp->OEMID[i]);
    kprintf("\n");
    kprintf("\tRevision: %d\n", rsdp->Revision);
    kprintf("\tRSDT Address: 0x%x\n", rsdp->RSDTAddress);
    kprintf("\tLength: %d\n", rsdp->Length);
    kprintf("\tXSDT Address: 0x%x\n", rsdp->XSDTAddress);
    kprintf("\tExtended checksum: %d\n\n", rsdp->ExtendedChecksum);

    ACPI::SDTHeader* xsdt = rsdp->GetRootSystemTable();
    uint64_t entries = (xsdt->Length - sizeof(ACPI::SDTHeader)) / 8;

    kprintf("System tables: ");
    for (uint64_t t = 0; t < entries; t++)
    {
        ACPI::SDTHeader* newSDTHeader = (ACPI::SDTHeader*)*(uint64_t*)((uint64_t)xsdt + sizeof(ACPI::SDTHeader) + (t * 8));
        for (int i = 0; i < 4; i++)
        {
            kprintf("%c", newSDTHeader->Signature[i]);
        }
        kprintf(" ");
    }

    kprintf("\n\n");

    ACPI::SDTHeader* mcfg = rsdp->GetSystemTable("MCFG");
    kprintf("\tMCFG Table: 0x%x\n", mcfg);
    ACPI::SDTHeader* apic = rsdp->GetSystemTable("APIC");
    kprintf("\tAPIC Table: 0x%x\n", apic);
    ACPI::SDTHeader* hpet = rsdp->GetSystemTable("HPET");
    kprintf("\tHPET Table: 0x%x\n", hpet);
    ACPI::SDTHeader* fadt = rsdp->GetSystemTable("FACP");
    kprintf("\tFADT Table: 0x%x\n\n", fadt);

    uint64_t usedMemory = PageFrameAllocator::SharedAllocator()->GetUsedRAM();
    uint64_t freeMemory = PageFrameAllocator::SharedAllocator()->GetFreeRAM();
    uint64_t reservedMemory = PageFrameAllocator::SharedAllocator()->GetReservedRAM();

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
}

MemoryMap _map(nullptr, 0);
MemoryMap* map = &_map;
void InitialiseDisplay(struct KernelInfo* info)
{
    fromUEFI = 1 - info->booted_from_BIOS;

    if(fromUEFI)
    {
        asm("cli");
        InitialiseGDT();
    }

    _map = MemoryMap(info->memMap, info->memMapEntries);

    InitialiseIDT();
    InitPaging(map);
    
    InitialiseDisplay(info->framebuffer, info->font);
    clrscr();
}

void InitialiseKernel(struct KernelInfo* info)
{
    PrintRSDPAndMemoryInfo(info);

    kprintf("Initialising the kernel heap!\n");
    InitialiseHeap((void*)0x0000100000000000, 0x10);
    InitialiseIRQ();
}