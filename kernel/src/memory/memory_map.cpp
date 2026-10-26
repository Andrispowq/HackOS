#include "memory_map.h"

#include "memory/bios_memory_type.h"
#include "memory/efi_memory_type.h"

extern bool fromUEFI;

MemoryMap::MemoryMap(MemoryMapEntry* entries, uint64_t count)
    : firstEntry(entries), entryCount(count),
      usableType(fromUEFI ? EFI_USUABLE_AREA : BIOS_USUABLE_AREA) {}

MemoryMap::~MemoryMap()
{
    delete[] usableMemory;
}

void MemoryMap::Reset(MemoryMapEntry* entries, uint64_t count)
{
    delete[] usableMemory;
    usableMemory = nullptr;
    firstEntry = entries;
    entryCount = count;
    usableType = fromUEFI ? EFI_USUABLE_AREA : BIOS_USUABLE_AREA;
}

bool MemoryMap::IsUsuableMemory(size_t index)
{
    if(index >= entryCount)
    {
        return false;
    }

    if((firstEntry + index)->type == usableType)
    {
        return true;
    }

    return false;
}

MemoryMapEntry** MemoryMap::GetUsuableMemory()
{
    if(usableMemory)
        return usableMemory;

    usableMemory = new MemoryMapEntry*[entryCount + 1];

    uint64_t counter = 0;
    for(uint64_t i = 0; i < entryCount; i++)
    {
        if(IsUsuableMemory(i))
        {
            usableMemory[counter++] = firstEntry + i;
        }
    }

    usableMemory[counter] = nullptr;
    return usableMemory;
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
