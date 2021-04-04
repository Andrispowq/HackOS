#ifndef PAGE_TABLE_MANAGER_H
#define PAGE_TABLE_MANAGER_H

#include "page_map_indexer.h"
#include "page_tables.h"
#include "page_frame_allocator.h"

class PageTableManager
{
public:
    PageTableManager(PageTable* PML4Address) : pml4(PML4Address) {}
    
    void MapMemory(vaddr_t virtualAddress, paddr_t physicalAddress);
    paddr_t PhysicalAddress(vaddr_t virtualAddress);

    void SetAsCurrent();
private:
    PageTable* pml4;
};

#endif