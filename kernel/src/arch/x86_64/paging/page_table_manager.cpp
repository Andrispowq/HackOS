#include "page_table_manager.h"

#include "lib/memory.h"

static void SetEntry(PageTableEntry* entry, PageTableFlagBits flags)
{
    entry->present = (flags & (uint64_t)PageTableFlags::Present) >> 0;
    entry->writeable = (flags & (uint64_t)PageTableFlags::Writable) >> 1;
    entry->user_access = (flags & (uint64_t)PageTableFlags::User) >> 2;
    entry->write_through = (flags & (uint64_t)PageTableFlags::WriteThrough) >> 3;
    entry->cache_disabled = (flags & (uint64_t)PageTableFlags::CacheDisabled) >> 4;
    entry->execution_disabled = (flags & (uint64_t)PageTableFlags::ExecutionDisabled) >> 63;
}

void PageTableManager::MapMemory(vaddr_t virtualAddress, paddr_t physicalAddress, PageTableFlagBits flags)
{
    PageMapIndexer indices(virtualAddress);

    PageTableEntry pml4_entry = pml4->entries[indices.index_PDP];
    PageTable* pdp;
    if(!pml4_entry.present)
    {
        pdp = (PageTable*)PageFrameAllocator::SharedAllocator()->RequestPage();
        memset(pdp, 0, 0x1000);
        pml4_entry.address = (uint64_t)pdp >> 12;
        SetEntry(&pml4_entry, flags);
        pml4->entries[indices.index_PDP] = pml4_entry;
    }
    else
    {
        pdp = (PageTable*)(uint64_t)(pml4_entry.address << 12);
    }

    PageTableEntry pdp_entry = pdp->entries[indices.index_PD];
    PageTable* pd;
    if(!pdp_entry.present)
    {
        pd = (PageTable*)PageFrameAllocator::SharedAllocator()->RequestPage();
        memset(pd, 0, 0x1000);
        pdp_entry.address = (uint64_t)pd >> 12;
        SetEntry(&pdp_entry, flags);
        pdp->entries[indices.index_PD] = pdp_entry;
    }
    else
    {
        pd = (PageTable*)(uint64_t)(pdp_entry.address << 12);
    }

    PageTableEntry pd_entry = pd->entries[indices.index_PT];
    PageTable* pt;
    if(!pd_entry.present)
    {
        pt = (PageTable*)PageFrameAllocator::SharedAllocator()->RequestPage();
        memset(pt, 0, 0x1000);
        pd_entry.address = (uint64_t)pt >> 12;
        SetEntry(&pd_entry, flags);
        pd->entries[indices.index_PT] = pd_entry;
    }
    else
    {
        pt = (PageTable*)(uint64_t)(pd_entry.address << 12);
    }

    PageTableEntry pt_entry = pt->entries[indices.index_P];
    pt_entry.address = physicalAddress >> 12;
    SetEntry(&pt_entry, flags);
    pt->entries[indices.index_P] = pt_entry;
}
    
paddr_t PageTableManager::PhysicalAddress(vaddr_t virtualAddress)
{
    PageMapIndexer indices(virtualAddress);

    PageTableEntry pml4_entry = pml4->entries[indices.index_PDP];
    PageTable* pdp = (PageTable*)(uint64_t)(pml4_entry.address << 12);

    PageTableEntry pdp_entry = pdp->entries[indices.index_PD];
    PageTable* pd = (PageTable*)(uint64_t)(pdp_entry.address << 12);

    PageTableEntry pd_entry = pd->entries[indices.index_PT];
    PageTable* pt = (PageTable*)(uint64_t)(pd_entry.address << 12);

    PageTableEntry pt_entry = pt->entries[indices.index_P];
    uint64_t address = pt_entry.address << 12;
    return address;
}

void PageTableManager::SetAsCurrent()
{
    asm volatile("mov %0, %%cr3" : : "r"(pml4));
}