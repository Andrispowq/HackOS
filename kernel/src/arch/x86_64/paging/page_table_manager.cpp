#include "page_table_manager.h"

#include "lib/memory.h"

extern PageTableManager KernelDirectory;
extern PageTableManager* CurrentDirectory;

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
    CurrentDirectory = this;
}

PageTableManager* PageTableManager::Clone()
{
    PageTable* newPML4 = ClonePML4(pml4, KernelDirectory.GetPML4());
    PageTableManager* manager = new PageTableManager(newPML4);
    return manager;
}

void* PageTableManager::ClonePage(void* src)
{
    void* addr = PageFrameAllocator::SharedAllocator()->RequestPage();
    memcpy(addr, src, 0x1000);
    return addr;
}

PageTable* PageTableManager::ClonePT(PageTable* src, PageTable* kernel)
{
    PageTable* page_dir = (PageTable*) kmalloc(sizeof(PageTable));
    memset(page_dir, 0, sizeof(PageTable));

    for(uint32_t i = 0; i < 512; i++)
    {
        if (src->entries[i].address == 0)
        {
            continue;
        }

        uint64_t entry = *(uint64_t*)(&src->entries[i]);
        uint64_t kernel_entry = *(uint64_t*)(&kernel->entries[i]);
        if(entry == kernel_entry)
        {
            page_dir->entries[i] = src->entries[i];
        }
        else
        {
            void* addr = (void*)(src->entries[i].address << 12);
            void* copy = ClonePage(addr);
            page_dir->entries[i].address = (uint64_t)copy >> 12;

            if (src->entries[i].present) page_dir->entries[i].present = 1;
            if (src->entries[i].writeable) page_dir->entries[i].writeable = 1;
            if (src->entries[i].user_access) page_dir->entries[i].user_access = 1;
            if (src->entries[i].write_through) page_dir->entries[i].write_through = 1;
            if (src->entries[i].accessed) page_dir->entries[i].accessed = 1;
            if (src->entries[i].dirty) page_dir->entries[i].dirty = 1;
            if (src->entries[i].size) page_dir->entries[i].size = 1;
            if (src->entries[i].global) page_dir->entries[i].global = 1;
            if (src->entries[i].execution_disabled) page_dir->entries[i].execution_disabled = 1;
        }
    }

    return page_dir;
}

PageTable* PageTableManager::ClonePD(PageTable* src, PageTable* kernel)
{
    PageTable* page_dir = (PageTable*) kmalloc(sizeof(PageTable));
    memset(page_dir, 0, sizeof(PageTable));

    for(uint32_t i = 0; i < 512; i++)
    {
        if (src->entries[i].address == 0)
        {
            continue;
        }

        uint64_t entry = *(uint64_t*)(&src->entries[i]);
        uint64_t kernel_entry = *(uint64_t*)(&kernel->entries[i]);
        if(entry == kernel_entry)
        {
            page_dir->entries[i] = src->entries[i];
        }
        else
        {
            PageTable* orig = (PageTable*)(src->entries[i].address << 12);
            PageTable* kern = (PageTable*)(kernel->entries[i].address << 12);
            PageTable* pt = ClonePT(orig, kern);
            page_dir->entries[i].address = (uint64_t)pt >> 12;

            if (src->entries[i].present) page_dir->entries[i].present = 1;
            if (src->entries[i].writeable) page_dir->entries[i].writeable = 1;
            if (src->entries[i].user_access) page_dir->entries[i].user_access = 1;
            if (src->entries[i].write_through) page_dir->entries[i].write_through = 1;
            if (src->entries[i].accessed) page_dir->entries[i].accessed = 1;
            if (src->entries[i].dirty) page_dir->entries[i].dirty = 1;
            if (src->entries[i].size) page_dir->entries[i].size = 1;
            if (src->entries[i].global) page_dir->entries[i].global = 1;
            if (src->entries[i].execution_disabled) page_dir->entries[i].execution_disabled = 1;
        }
    }

    return page_dir;
}

PageTable* PageTableManager::ClonePDP(PageTable* src, PageTable* kernel)
{
    PageTable* page_dir = (PageTable*) kmalloc(sizeof(PageTable));
    memset(page_dir, 0, sizeof(PageTable));

    for(uint32_t i = 0; i < 512; i++)
    {
        if (src->entries[i].address == 0)
        {
            continue;
        }

        uint64_t entry = *(uint64_t*)(&src->entries[i]);
        uint64_t kernel_entry = *(uint64_t*)(&kernel->entries[i]);
        if(entry == kernel_entry)
        {
            page_dir->entries[i] = src->entries[i];
        }
        else
        {
            PageTable* orig = (PageTable*)(src->entries[i].address << 12);
            PageTable* kern = (PageTable*)(kernel->entries[i].address << 12);
            PageTable* pt = ClonePD(orig, kern);
            page_dir->entries[i].address = (uint64_t)pt >> 12;

            if (src->entries[i].present) page_dir->entries[i].present = 1;
            if (src->entries[i].writeable) page_dir->entries[i].writeable = 1;
            if (src->entries[i].user_access) page_dir->entries[i].user_access = 1;
            if (src->entries[i].write_through) page_dir->entries[i].write_through = 1;
            if (src->entries[i].accessed) page_dir->entries[i].accessed = 1;
            if (src->entries[i].dirty) page_dir->entries[i].dirty = 1;
            if (src->entries[i].size) page_dir->entries[i].size = 1;
            if (src->entries[i].global) page_dir->entries[i].global = 1;
            if (src->entries[i].execution_disabled) page_dir->entries[i].execution_disabled = 1;
        }
    }

    return page_dir;
}

PageTable* PageTableManager::ClonePML4(PageTable* src, PageTable* kernel)
{
    PageTable* page_dir = (PageTable*) kmalloc(sizeof(PageTable));
    memset(page_dir, 0, sizeof(PageTable));

    for(uint32_t i = 0; i < 512; i++)
    {
        if (src->entries[i].address == 0)
        {
            continue;
        }

        uint64_t entry = *(uint64_t*)(&src->entries[i]);
        uint64_t kernel_entry = *(uint64_t*)(&kernel->entries[i]);
        if(entry == kernel_entry)
        {
            page_dir->entries[i] = src->entries[i];
        }
        else
        {
            PageTable* orig = (PageTable*)(src->entries[i].address << 12);
            PageTable* kern = (PageTable*)(kernel->entries[i].address << 12);
            PageTable* pt = ClonePDP(orig, kern);
            page_dir->entries[i].address = (uint64_t)pt >> 12;

            if (src->entries[i].present) page_dir->entries[i].present = 1;
            if (src->entries[i].writeable) page_dir->entries[i].writeable = 1;
            if (src->entries[i].user_access) page_dir->entries[i].user_access = 1;
            if (src->entries[i].write_through) page_dir->entries[i].write_through = 1;
            if (src->entries[i].accessed) page_dir->entries[i].accessed = 1;
            if (src->entries[i].dirty) page_dir->entries[i].dirty = 1;
            if (src->entries[i].size) page_dir->entries[i].size = 1;
            if (src->entries[i].global) page_dir->entries[i].global = 1;
            if (src->entries[i].execution_disabled) page_dir->entries[i].execution_disabled = 1;
        }
    }

    return page_dir;
}