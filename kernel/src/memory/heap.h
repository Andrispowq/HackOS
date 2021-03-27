#ifndef HEAP_H
#define HEAP_H

#include "lib/stdint.h"

typedef struct HeapSegmentHeader
{
    uint64_t length;
    struct HeapSegmentHeader *next, *last;
    uint8_t free;
} HeapSegmentHeader;

void InitialiseHeap(void* start_address, uint64_t size);

void* alloc(uint64_t size);
void free(void* address);

void ExpandHeap(uint64_t length);
void CombineForward(HeapSegmentHeader* header);
void CombineBackward(HeapSegmentHeader* header);
HeapSegmentHeader* Split(HeapSegmentHeader* header, size_t size);

#endif