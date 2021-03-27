#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"
#include "memory/heap.h"

/*
   C STANDARD LIBRARY CODE FOR THE KERNEL STARTS HERE
*/

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int memcmp(void* a, void* b, size_t n);

/*
   KERNEL HEAP CODE STARTS HERE
*/

uint64_t kmalloc_int(uint64_t size, int align, uint64_t* phys_addr);
void kfree(void* ptr);

uint64_t kmalloc_a(uint64_t size); // page aligned.
uint64_t kmalloc_p(uint64_t size, uint64_t* phys); // returns a physical address.
uint64_t kmalloc_ap(uint64_t size, uint64_t* phys); // page aligned.
uint64_t kmalloc(uint64_t size); // uint64_t kmalloc

#endif