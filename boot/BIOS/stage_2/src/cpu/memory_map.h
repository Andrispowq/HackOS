#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include "libc/stdint.h"

#include "hackos/boot_info.h"

extern uint8_t MemoryRegionCount;

MemoryMapEntry* GetMemoryRegions();

#endif
