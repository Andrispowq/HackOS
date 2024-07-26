#include "memory.h"

void* memcpy(void* dest, const void* src, uint64_t n)
{
    if(n <= 0)
        return dest;

    uint64_t sz = n;
    int i = 0;

    for (i; i < n; i++)
    {
        if((sz % 8) == 0)
        {
            *((uint64_t*)((uint8_t*)dest + i)) = *((uint64_t*)((uint8_t*)src + i));
            
            sz -= 8;
            i += 7;
        }
        else if((sz % 4) == 0)
        {
            *((uint32_t*)((uint8_t*)dest + i)) = *((uint32_t*)((uint8_t*)src + i));

            sz -= 4;
            i += 3;
        }
        else if((sz % 2) == 0)
        {
            *((uint16_t*)((uint8_t*)dest + i)) = *((uint16_t*)((uint8_t*)src + i));

            sz -= 2;
            i += 1;
        }
        else
        {
            *((uint8_t*)dest + i) = *((uint8_t*)src + i);

            sz -= 1;
        }
    }

    return dest;
}

void* memset(void* dest, uint32_t c, uint64_t n) 
{
    if(n <= 0)
        return dest;

    uint64_t sz = n;
    int i = 0;

    for (i; i < n; i++)
    {
        if((sz % 4) == 0)
        {
            *((uint32_t*)((uint8_t*)dest + i)) = (uint32_t)c;

            sz -= 4;
            i += 3;
        }
        else if((sz % 2) == 0)
        {
            *((uint16_t*)((uint8_t*)dest + i)) = (uint16_t)c;

            sz -= 2;
            i += 1;
        }
        else
        {
            *((uint8_t*)dest + i) = (uint8_t)c;
            sz -= 1;
        }
    }

    return dest;
}

int memcmp(void* a, void* b, uint64_t n)
{
    if(n <= 0)
        return -1;

    uint64_t sz = n;
    int i = 0;

    for (i; i < n; i++)
    {
        if((sz % 8) == 0)
        {
            if(*((uint64_t*)((uint8_t*)a + i)) != *((uint64_t*)((uint8_t*)b + i)))
            {
                return -1;
            }
            
            sz -= 8;
            i += 7;
        }
        else if((sz % 4) == 0)
        {
            if(*((uint32_t*)((uint8_t*)a + i)) != *((uint32_t*)((uint8_t*)b + i)))
            {
                return -1;
            }

            sz -= 4;
            i += 3;
        }
        else if((sz % 2) == 0)
        {
            if(*((uint16_t*)((uint8_t*)a + i)) != *((uint16_t*)((uint8_t*)b + i)))
            {
                return -1;
            }

            sz -= 2;
            i += 1;
        }
        else
        {
            if(*((uint8_t*)a + i) != *((uint8_t*)b + i))
            {
                return -1;
            }

            sz -= 1;
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
        free_mem_addr &= 0xFFFFF000;
        free_mem_addr += 0x1000;
    }

    /* Save also the physical address */
    if (phys_addr) 
        *phys_addr = free_mem_addr;
    
    uint32_t ret = free_mem_addr;
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