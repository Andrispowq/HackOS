#include "memory_map.h"

#include "memory/bios_memory_type.h"
#include "memory/efi_memory_type.h"

static MemoryMapEntry* usuableMemory[20];

static uint32_t usuableMemoryType = 0;
static bool usuableMemoryGot = 0;

extern bool fromUEFI;

bool MemoryMap::IsUsuableMemory(size_t index)
{
    if(index >= entryCount)
    {
        return false;
    }

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

    if((firstEntry + index)->type == usuableMemoryType)
    {
        return true;
    }

    return false;
}

MemoryMapEntry** MemoryMap::GetUsuableMemory()
{
    if(usuableMemoryGot)
        return usuableMemory;

    uint64_t counter = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        if(IsUsuableMemory(i))
        {
            usuableMemory[counter++] = firstEntry + i;
        }
    }

    usuableMemoryGot = true;
    return usuableMemory;
}

uint64_t MemoryMap::GetTotalSystemMemory()
{
    uint64_t total = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        MemoryMapEntry* entry = firstEntry + i;
        total += entry->size;
    }

    return total;
}

uint64_t MemoryMap::GetUsuableSystemMemory()
{
    uint64_t total = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        if(IsUsuableMemory(i))
        {
            MemoryMapEntry* entry = firstEntry + i;
            total += entry->size;
        }
    }

    return total;
}