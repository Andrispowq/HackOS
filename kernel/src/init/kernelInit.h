#ifndef KERNEL_INIT_H
#define KERNEL_INIT_H

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

void InitialiseDisplay(struct KernelInfo* info);
void InitialiseKernel(struct KernelInfo* info);

#endif