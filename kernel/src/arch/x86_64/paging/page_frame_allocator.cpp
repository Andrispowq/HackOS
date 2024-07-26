#include "page_frame_allocator.h"

#include "lib/stdio.h"
#include "lib/memory.h"

extern uint64_t kernelStart, kernelEnd;
extern uint8_t fromUEFI;

PageFrameAllocator* PageFrameAllocator::s_Allocator = nullptr;

void PageFrameAllocator::ReadMemoryMap(MemoryMap* memMap)
{
    void* largestFreeMemSeg = nullptr;
    size_t largestFreeMemSegSize = 0;

    for(uint64_t i = 0; i < memMap->GetEntryCount(); i++)
    {
        if(memMap->IsUsuableMemory(i))
        {
            MemoryMapEntry* entry = memMap->GetEntry(i);
            if(largestFreeMemSegSize < entry->size)
            {
                largestFreeMemSeg = (void*)entry->address;
                largestFreeMemSegSize = entry->size;
            }
        }
    }

    //We will reserve anything under 0x100000, and the kernel starts there
    //This means that the bitmap can not start under the kernel end, so we need to check that
    uint64_t bitmapStart = (uint64_t)largestFreeMemSeg;
    if(bitmapStart < kernelEnd)
    {
        bitmapStart = kernelEnd + 0x1000;
    }

    uint64_t memorySize = memMap->GetTotalSystemMemory();
    freeMemory = memorySize;
    usedMemory = 0;
    reservedMemory = 0;
    uint64_t frameCount = memorySize / 0x1000;
    frameCount /= 64;
    frameCount += 1;

    bitmap.SetBuffer((uint64_t*)bitmapStart);
    bitmap.SetSize(frameCount * sizeof(uint64_t));
    memset(bitmap.buffer, 0, frameCount * sizeof(uint64_t));

    ReservePages(0, memorySize / 0x1000 + 1);

    for(uint64_t i = 0; i < memMap->GetEntryCount(); i++)
    {
        if(memMap->IsUsuableMemory(i))
        {
            MemoryMapEntry* entry = memMap->GetEntry(i);
            UnreservePages((void*)entry->address, entry->size / 0x1000);
        }
    }

    //Lock the first 256 pages
    LockPages(0x0, 0x100);
    LockPages((void*)kernelStart, (kernelEnd - kernelStart + 0x1000) / 0x1000);

    ReservePages(bitmap.buffer, frameCount * sizeof(uint64_t) / 0x1000 + 1);
}

void* PageFrameAllocator::RequestPage()
{
    uint64_t index = bitmap.First();

    if(index == 0xFFFFFFFFFFFFFFFF)
    {
        kprintf("KERNEL ERROR: no more free pages are available!\n");
        while(1) asm("hlt");
    }

    LockPages((void*)(index * 0x1000), 1);
    return (void*)(index * 0x1000);
}

void PageFrameAllocator::ReservePages(void* address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        bitmap.Set(addr / 0x1000, true);
        freeMemory -= 0x1000;
        reservedMemory += 0x1000;
    }
}

void PageFrameAllocator::UnreservePages(void* address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        bitmap.Set(addr / 0x1000, false);
        freeMemory += 0x1000;
        reservedMemory -= 0x1000;
    }
}

void PageFrameAllocator::FreePages(void* address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

        bitmap.Set(addr / 0x1000, false);
        freeMemory += 0x1000;
        usedMemory -= 0x1000;
    }
}

void PageFrameAllocator::LockPages(void* address, uint64_t pageCount)
{
    for(uint32_t i = 0; i < pageCount; i++)
    {
        uint64_t addr = (uint64_t)address + i * 0x1000;

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