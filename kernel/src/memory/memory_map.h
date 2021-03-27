#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include "lib/stdint.h"

typedef struct memory_map_entry
{
    uint64_t address;
    uint64_t size;
    uint32_t type;
    uint32_t attributes;
} MemoryMapEntry;

uint8_t IsUsuableMemory(MemoryMapEntry* entry);
MemoryMapEntry** GetUsuableMemory(MemoryMapEntry* firstEntry, uint64_t entryCount);

uint64_t GetTotalSystemMemory(MemoryMapEntry* firstEntry, uint64_t entryCount);
uint64_t GetUsuableSystemMemory(MemoryMapEntry* firstEntry, uint64_t entryCount);

#endif