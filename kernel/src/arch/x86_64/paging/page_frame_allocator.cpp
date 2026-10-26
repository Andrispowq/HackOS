#include "page_frame_allocator.h"

#include "lib/stdio.h"
#include "lib/memory.h"

extern uint64_t kernelStart, kernelEnd;
extern uint64_t free_mem_addr;

PageFrameAllocator* PageFrameAllocator::s_Allocator = nullptr;

void PageFrameAllocator::ReadMemoryMap(MemoryMap* memMap)
{
    uint64_t highest = 0;
    for (uint64_t i = 0; i < memMap->GetEntryCount(); ++i) {
        auto entry = memMap->GetEntry(i);
        if (memMap->IsUsuableMemory(i) && entry->size <= ~0ULL - entry->address &&
            entry->address + entry->size > highest) highest = entry->address + entry->size;
    }
    uint64_t pages = highest / 4096;
    uint64_t bytes = ((pages + 63) / 64) * sizeof(uint64_t);
    uint64_t bitmapStart = 0;
    // Keep the kernel and its early bump allocations out of the free-page pool.
    uint64_t protectedEnd = (free_mem_addr + 4095) & ~4095ULL;
    for (uint64_t i = 0; i < memMap->GetEntryCount(); ++i) {
        auto entry = memMap->GetEntry(i);
        if (!memMap->IsUsuableMemory(i) || entry->size > ~0ULL - entry->address) continue;
        uint64_t start = (entry->address + 4095) & ~4095ULL;
        if (start < protectedEnd) start = protectedEnd;
        uint64_t end = entry->address + entry->size;
        if (start <= end && bytes <= end - start) { bitmapStart = start; break; }
    }
    if (!bitmapStart || !bytes) { for (;;) asm("cli; hlt"); }
    bitmap.SetBuffer((uint64_t*)bitmapStart);
    bitmap.SetSize(bytes);
    memset(bitmap.buffer, 0xff, bytes);
    freeMemory = usedMemory = 0;
    reservedMemory = pages * 4096;
    for (uint64_t i = 0; i < memMap->GetEntryCount(); ++i) {
        auto entry = memMap->GetEntry(i);
        if (!memMap->IsUsuableMemory(i) || entry->size > ~0ULL - entry->address) continue;
        uint64_t start = (entry->address + 4095) & ~4095ULL;
        uint64_t end = (entry->address + entry->size) & ~4095ULL;
        if (end > start) UnreservePages((void*)start, (end - start) / 4096);
    }
    ReservePages(nullptr, 256);
    LockPages((void*)kernelStart, (protectedEnd - kernelStart) / 4096);
    ReservePages(bitmap.buffer, (bytes + 4095) / 4096);
}

void* PageFrameAllocator::TryRequestPage()
{
    uint64_t index = bitmap.First();
    if (index == ~0ULL) return nullptr;
    LockPages((void*)(index * 4096), 1);
    return (void*)(index * 4096);
}

void* PageFrameAllocator::RequestPage()
{
    if (void* page = TryRequestPage()) return page;
    kprintf("KERNEL ERROR: no more free pages are available!\n");
    for (;;) asm("hlt");
}

void PageFrameAllocator::ReservePages(void* address, uint64_t pageCount)
{
    for(uint64_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        if (addr / 0x1000 >= bitmap.size * 8 || bitmap.Get(addr / 0x1000)) continue;
        bitmap.Set(addr / 0x1000, true);
        freeMemory -= 0x1000;
        reservedMemory += 0x1000;
    }
}

void PageFrameAllocator::UnreservePages(void* address, uint64_t pageCount)
{
    for(uint64_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        if (addr / 0x1000 >= bitmap.size * 8 || !bitmap.Get(addr / 0x1000)) continue;
        bitmap.Set(addr / 0x1000, false);
        freeMemory += 0x1000;
        reservedMemory -= 0x1000;
    }
}

void PageFrameAllocator::FreePages(void* address, uint64_t pageCount)
{
    for(uint64_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        if (addr / 0x1000 >= bitmap.size * 8 || !bitmap.Get(addr / 0x1000)) continue;
        bitmap.Set(addr / 0x1000, false);
        freeMemory += 0x1000;
        usedMemory -= 0x1000;
    }
}

void PageFrameAllocator::LockPages(void* address, uint64_t pageCount)
{
    for(uint64_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        if (addr / 0x1000 >= bitmap.size * 8 || bitmap.Get(addr / 0x1000)) continue;
        bitmap.Set(addr / 0x1000, true);
        freeMemory -= 0x1000;
        usedMemory += 0x1000;
    }
}

PageFrameAllocator allocator;
PageFrameAllocator* PageFrameAllocator::SharedAllocator()
{
    if(s_Allocator == nullptr)
    {
        s_Allocator = &allocator;
    }

    return s_Allocator;
}