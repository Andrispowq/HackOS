#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include "libc/stdint.h"

typedef struct memory_map_entry
{
    uint64_t address;
    uint64_t length;
    uint32_t type;
    uint32_t extended_attributes;
} MemoryMapEntry;

extern uint8_t MemoryRegionCount;

MemoryMapEntry* GetMemoryRegions();

#endif