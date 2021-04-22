#include "heap.h"
#include "arch/x86_64/paging/paging.h"

void* heapStart;
void* heapEnd;
HeapSegmentHeader* LastHdr;

extern uint8_t heap_init;

extern PageTableManager KernelDirectory;
void InitialiseHeap(void* start_address, uint64_t size)
{
    void* pos = start_address;

    for (uint64_t i = 0; i < size; i++)
    {
        KernelDirectory.MapMemory((uint64_t)pos, 
            (paddr_t)PageFrameAllocator::SharedAllocator()->RequestPage());
        pos = (void*)((uint64_t)pos + 0x1000);
    }

    size_t heapLength = size * 0x1000;

    heapStart = start_address;
    heapEnd = (void*)((uint64_t)start_address + heapLength);
    HeapSegmentHeader* startSeg = (HeapSegmentHeader*)start_address;
    startSeg->length = heapLength - sizeof(HeapSegmentHeader);
    startSeg->next = nullptr;
    startSeg->last = nullptr;
    startSeg->free = true;
    LastHdr = startSeg;

    heap_init = 1;
}

void* alloc(size_t size)
{
    // it is not a multiple of 0x10
    if (size % 0x10 > 0)
    { 
        size -= (size % 0x10);
        size += 0x10;
    }

    if (size == 0) 
    {
        return nullptr;
    }

    HeapSegmentHeader* currentSeg = (HeapSegmentHeader*) heapStart;
    while(1)
    {
        if(currentSeg->free)
        {
            if (currentSeg->length > size)
            {
                currentSeg->Split(size);
                currentSeg->free = 0;
                return (void*)((uint64_t)currentSeg + sizeof(HeapSegmentHeader));
            }

            if (currentSeg->length == size)
            {
                currentSeg->free = 0;
                return (void*)((uint64_t)currentSeg + sizeof(HeapSegmentHeader));
            }
        }

        if (currentSeg->next == nullptr) 
        {
            break;
        }

        currentSeg = currentSeg->next;
    }

    ExpandHeap(size);
    return alloc(size);
}

void free(void* address)
{
    HeapSegmentHeader* segment = (HeapSegmentHeader*)address - 1;
    segment->free = 1;
    segment->CombineForward();
    segment->CombineBackward();
}

void ExpandHeap(size_t length)
{
    if (length % 0x1000) 
    {
        length -= length % 0x1000;
        length += 0x1000;
    }

    size_t pageCount = length / 0x1000;
    HeapSegmentHeader* newSegment = (HeapSegmentHeader*)heapEnd;

    for (size_t i = 0; i < pageCount; i++)
    {
        KernelDirectory.MapMemory((vaddr_t)heapEnd, 
            (paddr_t)PageFrameAllocator::SharedAllocator()->RequestPage());
        heapEnd = (void*)((size_t)heapEnd + 0x1000);
    }

    newSegment->free = 1;
    newSegment->last = LastHdr;
    LastHdr->next = newSegment;
    LastHdr = newSegment;
    newSegment->next = nullptr;
    newSegment->length = length - sizeof(HeapSegmentHeader);
    newSegment->CombineBackward();
}

void HeapSegmentHeader::CombineForward()
{
    if (next == nullptr) 
    {
        return;
    }

    if (!next->free) 
    {
        return;
    }

    if (next == LastHdr) 
    {
        LastHdr = this;
    }

    if (next->next != nullptr)
    {
        next->next->last = this;
    }

    next = next->next;

    length = length + next->length + sizeof(HeapSegmentHeader);
}

void HeapSegmentHeader::CombineBackward()
{
    if (last != nullptr && last->free) 
    {
        last->CombineForward();
    }
}

HeapSegmentHeader* HeapSegmentHeader::Split(size_t size)
{
    if (size < 0x10) 
    {
        return nullptr;
    }

    int64_t splitSegLength = length - size - (sizeof(HeapSegmentHeader));
    if (splitSegLength < 0x10) 
    {
        return nullptr;
    }

    HeapSegmentHeader* newSplitHdr = (HeapSegmentHeader*) ((size_t)this + size + sizeof(HeapSegmentHeader));
    next->last = newSplitHdr; // Set the next segment's last segment to our new segment
    newSplitHdr->next = next; // Set the new segment's next segment to out original next segment
    next = newSplitHdr; // Set our new segment to the new segment
    newSplitHdr->last = this; // Set our new segment's last segment to the current segment
    newSplitHdr->length = splitSegLength; // Set the new header's length to the calculated value
    newSplitHdr->free = free; // make sure the new segment's free is the same as the original
    length = size; // set the length of the original segment to its new length

    if (LastHdr == this) 
    { 
        LastHdr = newSplitHdr;
    }

    return newSplitHdr;
}

#include "lib/memory.h"

void* operator new(unsigned long int size) { return (void*)kmalloc((uint64_t)size); }
void* operator new[](unsigned long int size) { return (void*)kmalloc((uint64_t)size); }

void operator delete(void* ptr) { return kfree(ptr); }
void operator delete[](void* ptr) { return kfree(ptr); }

void operator delete(void* ptr, unsigned long int size) { return kfree(ptr); }
void operator delete[](void* ptr, unsigned long int size) { return kfree(ptr); }