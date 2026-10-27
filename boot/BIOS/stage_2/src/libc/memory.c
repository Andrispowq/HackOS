#include "memory.h"

void* memcpy(void* dest, const void* src, uint64_t n)
{
    uint8_t* output = (uint8_t*)dest;
    const uint8_t* input = (const uint8_t*)src;
    for (uint64_t i = 0; i < n; ++i)
    {
        output[i] = input[i];
    }

    return dest;
}

void* memset(void* dest, uint32_t c, uint64_t n)
{
    uint8_t* output = (uint8_t*)dest;
    for (uint64_t i = 0; i < n; ++i)
    {
        output[i] = (uint8_t)c;
    }

    return dest;
}

int memcmp(void* a, void* b, uint64_t n)
{
    const uint8_t* left = (const uint8_t*)a;
    const uint8_t* right = (const uint8_t*)b;
    for (uint64_t i = 0; i < n; ++i)
    {
        if (left[i] != right[i])
        {
            return (int)left[i] - (int)right[i];
        }
    }

    return 0;
}

extern uint64_t end;
extern uint64_t free_mem_addr;

uint64_t kmalloc_int(uint64_t size, int align, uint64_t* phys_addr)
{
    /* Pages are aligned to 4K, or 0x1000 */
    if (align == 1 && (free_mem_addr & 0x00000FFF))
    {
        free_mem_addr &= ~0xFFFULL;
        free_mem_addr += 0x1000;
    }

    /* Save also the physical address */
    if (phys_addr)
    {
        *phys_addr = free_mem_addr;
    }

    uint64_t ret = free_mem_addr;
    free_mem_addr += size; /* Remember to increment the pointer */
    return ret;
}

void kfree(void* ptr)
{
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
