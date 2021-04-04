#ifndef PAGING_H
#define PAGING_H

#include "arch/x86_64/interrupts/idt.h"

#include "page_table_manager.h"
#include "memory/memory_map.h"

void InitPaging(MemoryMap* memMap);
void PageFaultHandler(Registers* regs);

#endif