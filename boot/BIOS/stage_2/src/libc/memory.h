#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"

/*
   C STANDARD LIBRARY CODE FOR THE KERNEL STARTS HERE
*/

void* memcpy(void* dest, const void* src, uint64_t n);
void* memset(void* dest, uint32_t c, uint64_t n);
int memcmp(void* a, void* b, uint64_t n);

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