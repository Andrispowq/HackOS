#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

#include "drivers/screen/screen.h"

#include "arch/x86_64/interrupts/idt.h"
#include "arch/x86_64/gdt/gdt.h"
#include "arch/x86_64/timer/pit.h"
#include "arch/x86_64/paging/paging.h"
#include "arch/x86_64/ports.h"

#include "memory/memory_map.h"
#include "memory/heap.h"

#include "acpi/rsdp.h"
#include "acpi/system_table.h"

#include "lib/stdio.h"
#include "lib/font/font.h"

struct KernelInfo
{
    Framebuffer framebuffer;
    PSF1_FONT* font;
    MemoryMapEntry* memMap;
    uint64_t memMapEntrySize;
    RSDP* rsdp;
    uint8_t memMapEntries;
    uint8_t rsdp_version;
    uint8_t booted_from_BIOS;
};

void InitialiseDisplay(struct KernelInfo* info);
void InitialiseKernel(struct KernelInfo* info);

#endif