#include "hackos/runtime/memory.h"

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

