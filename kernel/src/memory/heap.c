#include "heap.h"
#include "arch/paging/paging.h"

void* heapStart;
void* heapEnd;
HeapSegmentHeader* LastHdr;

extern uint8_t heap_init;

void InitialiseHeap(void* start_address, uint64_t size)
{
    void* pos = start_address;

    for (uint64_t i = 0; i < size; i++)
    {
        MapMemory((uint64_t)pos, RequestPage());
        pos = (void*)((uint64_t)pos + 0x1000);
    }

    size_t heapLength = size * 0x1000;

    heapStart = start_address;
    heapEnd = (void*)((uint64_t)start_address + heapLength);
    HeapSegmentHeader* startSeg = (HeapSegmentHeader*)start_address;
    startSeg->length = heapLength - sizeof(HeapSegmentHeader);
    startSeg->next = NULL;
    startSeg->last = NULL;
    startSeg->free = 1;
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
        return NULL;
    }

    HeapSegmentHeader* currentSeg = (HeapSegmentHeader*) heapStart;
    while(1)
    {
        if(currentSeg->free)
        {
            if (currentSeg->length > size)
            {
                Split(currentSeg, size);
                currentSeg->free = 0;
                return (void*)((uint64_t)currentSeg + sizeof(HeapSegmentHeader));
            }

            if (currentSeg->length == size)
            {
                currentSeg->free = 0;
                return (void*)((uint64_t)currentSeg + sizeof(HeapSegmentHeader));
            }
        }

        if (currentSeg->next == NULL) 
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
    CombineForward(segment);
    CombineBackward(segment);
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
        MapMemory(heapEnd, RequestPage());
        heapEnd = (void*)((size_t)heapEnd + 0x1000);
    }

    newSegment->free = 1;
    newSegment->last = LastHdr;
    LastHdr->next = newSegment;
    LastHdr = newSegment;
    newSegment->next = NULL;
    newSegment->length = length - sizeof(HeapSegmentHeader);
    CombineBackward(newSegment);
}

void CombineForward(HeapSegmentHeader* header)
{
    if (header->next == NULL) 
    {
        return;
    }

    if (!header->next->free) 
    {
        return;
    }

    if (header->next == LastHdr) 
    {
        LastHdr = header;
    }

    if (header->next->next != NULL)
    {
        header->next->next->last = header;
    }

    header->next = header->next->next;

    header->length = header->length + header->next->length + sizeof(HeapSegmentHeader);
}

void CombineBackward(HeapSegmentHeader* header)
{
    if (header->last != NULL && header->last->free) 
    {
        CombineForward(header->last);
    }
}

HeapSegmentHeader* Split(HeapSegmentHeader* header, size_t size)
{
    if (size < 0x10) 
    {
        return NULL;
    }

    int64_t splitSegLength = header->length - size - (sizeof(HeapSegmentHeader));
    if (splitSegLength < 0x10) 
    {
        return NULL;
    }

    HeapSegmentHeader* newSplitHdr = (HeapSegmentHeader*) ((size_t)header + size + sizeof(HeapSegmentHeader));
    header->next->last = newSplitHdr; // Set the next segment's last segment to our new segment
    newSplitHdr->next = header->next; // Set the new segment's next segment to out original next segment
    header->next = newSplitHdr; // Set our new segment to the new segment
    newSplitHdr->last = header; // Set our new segment's last segment to the current segment
    newSplitHdr->length = splitSegLength; // Set the new header's length to the calculated value
    newSplitHdr->free = header->free; // make sure the new segment's free is the same as the original
    header->length = size; // set the length of the original segment to its new length

    if (LastHdr == header) 
    { 
        LastHdr = newSplitHdr;
    }

    return newSplitHdr;
}