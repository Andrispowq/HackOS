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

#include "proc/elf/elf.h"

#include "lib/stdio.h"
#include "lib/font/font.h"

#include "hackos/boot_info.h"

void InitialiseDisplay(struct KernelInfo* info);
void InitialiseKernel(struct KernelInfo* info);

#endif
