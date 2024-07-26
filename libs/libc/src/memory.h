#ifndef MEMORY_H
#define MEMORY_H

#include "stdint.h"

void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* dest, int c, size_t n);
int memcmp(void* a, void* b, size_t n);

uint64_t malloc_int(uint64_t size, int align, uint64_t* phys_addr);
void free(void* ptr);

uint64_t malloc_a(uint64_t size); // page aligned.
uint64_t malloc_p(uint64_t size, uint64_t* phys); // returns a physical address.
uint64_t malloc_ap(uint64_t size, uint64_t* phys); // page aligned.
uint64_t malloc(uint64_t size); // uint64_t kmalloc

#endif