#ifndef PAGE_FRAME_ALLOCATOR
#define PAGE_FRAME_ALLOCATOR

#include "lib/data_structures/bitmap.h"

#include "memory/memory_map.h"

class PageFrameAllocator
{
public:
    void ReadMemoryMap(MemoryMap* memMap);

    void FreePages(void* address, uint64_t pageCount);
    void LockPages(void* address, uint64_t pageCount);

    void ReservePages(void* address, uint64_t pageCount);
    void UnreservePages(void* address, uint64_t pageCount);

    void* RequestPage();

    uint64_t GetFreeRAM() const { return freeMemory; }
    uint64_t GetUsedRAM() const { return usedMemory; }
    uint64_t GetReservedRAM() const { return reservedMemory; }

    static PageFrameAllocator* SharedAllocator();

private:
    static PageFrameAllocator* s_Allocator;
    Bitmap bitmap;

    uint64_t freeMemory;
    uint64_t usedMemory;
    uint64_t reservedMemory;
};

#endif