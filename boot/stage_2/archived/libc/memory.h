#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"
#include "data_structures/ordered_array.h"

/*
   C STANDARD LIBRARY CODE FOR THE KERNEL STARTS HERE
*/

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);

/*
   KERNEL HEAP CODE STARTS HERE
*/

#define KHEAP_START 0xC0000000
#define KHEAP_INITIAL_SIZE 0x100000
#define HEAP_INDEX_SIZE 0x20000
#define HEAP_MAGIC 0x123890AB
#define HEAP_MIN_SIZE 0x70000

uint32_t kmalloc_int(size_t size, int align, uint32_t* phys_addr);
void kfree(void* ptr);

uint32_t kmalloc_a(size_t size); // page aligned.
uint32_t kmalloc_p(size_t size, uint32_t* phys); // returns a physical address.
uint32_t kmalloc_ap(size_t size, uint32_t* phys); // page aligned.
uint32_t kmalloc(size_t size); // normal kmalloc

/**
  Size information for a hole/block
**/
typedef struct
{
   uint32_t magic;   // Magic number, used for error checking and identification.
   uint8_t is_hole;   // 1 if this is a hole. 0 if this is a block.
   uint32_t size;    // size of the block, including the end footer.
} header_t;

typedef struct
{
   uint32_t magic;     // Magic number, same as in header_t.
   header_t *header; // Pointer to the block header.
} footer_t;

typedef struct
{
   ordered_array_t index;
   uint32_t start_address; // The start of our allocated space.
   uint32_t end_address;   // The end of our allocated space. May be expanded up to max_address.
   uint32_t max_address;   // The maximum address the heap can be expanded to.
   uint8_t supervisor;     // Should extra pages requested by us be mapped as supervisor-only?
   uint8_t readonly;       // Should extra pages requested by us be mapped as read-only?
} heap_t;

/**
  Create a new heap.
**/
heap_t* create_heap(uint32_t start, uint32_t end, uint32_t max, uint8_t supervisor, uint8_t readonly);

/**
  Allocates a contiguous region of memory 'size' in size. If page_align == 1, it creates that block starting
  on a page boundary.
**/
void* alloc(uint32_t size, uint8_t page_align, heap_t* heap);

/**
   Releases a block allocated with 'alloc'.
**/
void free(void *p, heap_t *heap);

#endif