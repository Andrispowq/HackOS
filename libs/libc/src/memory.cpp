#include "memory.h"

#include "kernel_interface/syscall.h"

extern "C" void* __memset(void* dest, int val, size_t n);
extern "C" void* __memcpy(void* dest, const void* src, size_t n);
extern "C" int __memcmp(void* dest, const void* src, size_t n);

void* memcpy(void* dest, const void* src, size_t n)
{
    return __memcpy(dest, src, n);
}

void* memset(void* dest, int c, size_t n) 
{
    return __memset(dest, c, n);
}

int memcmp(void* a, void* b, size_t n)
{
    return __memcmp(a, b, n);
}

uint64_t malloc_int(uint64_t size, int align, uint64_t* phys_addr)
{
    return syscall_kmalloc_int(size, align, phys_addr);
}

void free(void* ptr)
{
    syscall_kfree(ptr);
}

uint64_t malloc_a(uint64_t size)
{
    return malloc_int(size, 1, 0);
}

uint64_t malloc_p(uint64_t size, uint64_t* phys)
{
    return malloc_int(size, 0, phys);
}

uint64_t malloc_ap(uint64_t size, uint64_t* phys)
{
    return malloc_int(size, 1, phys);
}

uint64_t malloc(uint64_t size)
{
    return malloc_int(size, 0, 0);
}