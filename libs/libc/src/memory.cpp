#include "memory.h"

#include "kernel_interface/syscall.h"

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