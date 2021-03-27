#include "memory_map.h"

#include "memory/bios_memory_type.h"
#include "memory/efi_memory_type.h"

MemoryMapEntry* usuableMemory[20];
static uint32_t usuableMemoryType = 0;
static uint8_t usuableMemoryGot = 0;

extern uint8_t fromUEFI;

uint8_t IsUsuableMemory(MemoryMapEntry* entry)
{
    if(usuableMemoryType == 0)
    {
        if(fromUEFI)
        {
            usuableMemoryType = EFI_USUABLE_AREA;
        }
        else
        {
            usuableMemoryType = BIOS_USUABLE_AREA;
        }
    }

    if(entry->type == usuableMemoryType)
    {
        return 1;
    }

    return 0;
}

MemoryMapEntry** GetUsuableMemory(MemoryMapEntry* firstEntry, uint64_t entryCount)
{
    if(usuableMemoryGot)
        return usuableMemory;

    uint64_t counter = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        MemoryMapEntry* entry = firstEntry + i;
        if(IsUsuableMemory(entry))
        {
            usuableMemory[counter++] = entry;
        }
    }

    usuableMemoryGot = 1;
    return usuableMemory;
}

uint64_t GetTotalSystemMemory(MemoryMapEntry* firstEntry, uint64_t entryCount)
{
    uint64_t total = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        MemoryMapEntry* entry = firstEntry + i;
        total += entry->size;
    }

    return total;
}

uint64_t GetUsuableSystemMemory(MemoryMapEntry* firstEntry, uint64_t entryCount)
{
    uint64_t total = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        MemoryMapEntry* entry = firstEntry + i;
        if(IsUsuableMemory(entry))
        {
            total += entry->size;
        }
    }

    return total;
}