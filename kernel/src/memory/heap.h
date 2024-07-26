#ifndef HEAP_H
#define HEAP_H

#include "lib/stdint.h"

typedef struct HeapSegmentHeader
{
    uint64_t length;
    struct HeapSegmentHeader *next, *last;
    bool free;

    void CombineForward();
    void CombineBackward();
    HeapSegmentHeader* Split(size_t size);
} HeapSegmentHeader;

void InitialiseHeap(void* start_address, uint64_t size);
void ExpandHeap(uint64_t length);

void* alloc(uint64_t size, uint64_t alignment = 0);
void free(void* address);

void* operator new(unsigned long int size);
void* operator new[](unsigned long int size);

void operator delete(void* ptr);
void operator delete[](void* ptr);

void operator delete(void* ptr, unsigned long int size);
void operator delete[](void* ptr, unsigned long int size);

#endif