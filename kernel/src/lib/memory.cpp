#include "memory.h"
#include "arch/x86_64/paging/paging.h"

extern "C" void __memset(void* dest, int val, size_t n);
extern "C" void __memcpy(void* dest, const void* src, size_t n);
extern "C" int __memcmp(void* dest, const void* src, size_t n);

void* memcpy(void* dest, const void* src, size_t n)
{
    if(n <= 0)
        return dest;

    __memcpy(dest, src, n);
    return dest;
}

void* memset(void* dest, int c, size_t n) 
{
    if(n <= 0)
        return dest;

    __memset(dest, c, n);
    return dest;
}

int memcmp(void* a, void* b, size_t n)
{
    if(n <= 0)
        return -1;

    return (__memcmp(a, b, n) == 0);
}

extern uint64_t end;
uint64_t free_mem_addr = (uint64_t)&end;

uint8_t heap_init = 0;

extern PageTableManager KernelDirectory;
uint64_t kmalloc_int(uint64_t size, int align, uint64_t* phys_addr)
{
    if(heap_init == 0)
    {
        if (align == 1 && (free_mem_addr & 0x00000FFF)) 
        {
            free_mem_addr &= 0xFFFFF000;
            free_mem_addr += 0x1000;
        }

        if (phys_addr) 
        {
            *phys_addr = free_mem_addr;
        }

        uint64_t ret = free_mem_addr;
        free_mem_addr += size;

        return ret;
    }
    else
    {
        void* addr = alloc(size);

        if (phys_addr != 0)
        {
            uint64_t phys = KernelDirectory.PhysicalAddress((uint64_t)addr);
            *phys_addr = phys + (uint64_t)addr & 0xFFF;
        }

        return (uint64_t)addr;
    }
}

void kfree(void* ptr)
{
    if(heap_init == 1)
    {
        free(ptr);
    }
}

uint64_t kmalloc_a(uint64_t size)
{
    return kmalloc_int(size, 1, 0);
}

uint64_t kmalloc_p(uint64_t size, uint64_t* phys)
{
    return kmalloc_int(size, 0, phys);
}

uint64_t kmalloc_ap(uint64_t size, uint64_t* phys)
{
    return kmalloc_int(size, 1, phys);
}

uint64_t kmalloc(uint64_t size)
{
    return kmalloc_int(size, 0, 0);
}