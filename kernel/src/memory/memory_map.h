#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include "lib/stdint.h"

#include "hackos/boot_info.h"

class MemoryMap
{
  public:
    MemoryMap(MemoryMapEntry* firstEntry, uint64_t entryCount);
    ~MemoryMap();
    MemoryMap(const MemoryMap&) = delete;
    MemoryMap& operator=(const MemoryMap&) = delete;

    void Reset(MemoryMapEntry* entries, uint64_t count);

    bool IsUsuableMemory(size_t entry);
    MemoryMapEntry** GetUsuableMemory();

    uint64_t GetTotalSystemMemory();
    uint64_t GetUsuableSystemMemory();

    uint64_t GetEntryCount() const
    {
        return entryCount;
    }

    MemoryMapEntry* GetEntry(size_t index) const
    {
        return firstEntry + index;
    }

  public:
    MemoryMapEntry* firstEntry;
    uint64_t entryCount;

  private:
    uint32_t usableType;
    MemoryMapEntry** usableMemory = nullptr;
};

#endif
