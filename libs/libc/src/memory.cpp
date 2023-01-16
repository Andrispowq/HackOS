#include "memory.h"

#include "kernel_interface/syscall.h"

extern "C" void __memset(void* dest, int val, size_t n);
extern "C" void __memcpy(void* dest, const void* src, size_t n);
extern "C" int __memcmp(void* dest, const void* src, size_t n);

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

    //__memcpy(dest, src, n);
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

    //__memset(dest, c, n);
    return dest;
}

int memcmp(void* a, void* b, size_t n)
{
    if(n <= 0)
        return -1;

    return (__memcmp(a, b, n) == 0);
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