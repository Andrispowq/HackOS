#ifndef PAGE_TABLE_MANAGER_H
#define PAGE_TABLE_MANAGER_H

#include "page_map_indexer.h"
#include "page_tables.h"
#include "page_frame_allocator.h"

enum class PageTableFlags : uint64_t
{
    Present = 1 << 0,
    Writable = 1 << 1,
    User = 1 << 2,
    WriteThrough = 1 << 3,
    CacheDisabled = 1 << 4,
    ExecutionDisabled = 1ULL << 63ULL
};

typedef uint64_t PageTableFlagBits;

class PageTableManager
{
public:
    PageTableManager(PageTable* PML4Address) : pml4(PML4Address) {}
    
    void MapMemory(vaddr_t virtualAddress, paddr_t physicalAddress, PageTableFlagBits flags = 0x3);
    paddr_t PhysicalAddress(vaddr_t virtualAddress);

    void SetAsCurrent();
private:
    PageTable* pml4;
};

#endif