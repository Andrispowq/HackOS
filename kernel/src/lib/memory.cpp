#include "memory.h"
#include "arch/x86_64/paging/paging.h"

void* memcpy(void* dest, const void* src, size_t n)
{
    if(n <= 0)
        return dest;

    size_t sz = n;
    size_t i = 0;

    for (i; i < n;)
    {
        uint64_t dest_curr = (uint64_t)dest + i;
        uint64_t src_curr = (uint64_t)src + i;

        if((sz >= 8) && !(dest_curr & 0x7) && !(src_curr & 0x7))
        {
            *(uint64_t*)dest_curr = *(uint64_t*)src_curr;
            
            sz -= 8;
            i += 8;
        }
        else if((sz >= 4) && !(dest_curr & 0x3) && !(src_curr & 0x3))
        {
            *(uint32_t*)dest_curr = *(uint32_t*)src_curr;

            sz -= 4;
            i += 4;
        }
        else if((sz >= 2) && !(dest_curr & 0x1) && !(src_curr & 0x1))
        {
            *(uint16_t*)dest_curr = *(uint16_t*)src_curr;

            sz -= 2;
            i += 2;
        }
        else
        {
            *(uint8_t*)dest_curr = *(uint8_t*)src_curr;

            sz -= 1;
            i += 1;
        }
    }

    return dest;

}

void* memset(void* dest, int c, size_t n) 
{
    if(n <= 0)
        return dest;

    size_t sz = n;
    size_t i = 0;

    for (i; i < n;)
    {
        uint64_t dest_curr = (uint64_t)dest + i;

        if((sz >= 4) && !(dest_curr & 0x3))
        {
            *(uint32_t*)dest_curr = (uint32_t)c;

            sz -= 4;
            i += 4;
        }
        else if((sz >= 2) && !(dest_curr & 0x1))
        {
            *(uint16_t*)dest_curr = (uint16_t)c;

            sz -= 2;
            i += 2;
        }
        else
        {
            *(uint8_t*)dest_curr = (uint8_t)c;

            sz -= 1;
            i += 1;
        }
    }

    return dest;
}

int memcmp(void* a, void* b, size_t n)
{
    if(n <= 0)
        return -1;

    size_t sz = n;
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
            *phys_addr = phys + ((uint64_t)addr & 0xFFF);
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